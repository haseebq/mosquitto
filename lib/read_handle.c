/*
Copyright (c) 2009,2010, Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <mosquitto.h>
#include <logging_mosq.h>
#include <messages_mosq.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>
#include <read_handle.h>
#include <send_mosq.h>
#include <util_mosq.h>

int _mosquitto_packet_handle(struct mosquitto *mosq)
{
	if(!mosq) return 1;

	switch((mosq->in_packet.command)&0xF0){
		case PINGREQ:
			return _mosquitto_handle_pingreq(mosq);
		case PINGRESP:
			return _mosquitto_handle_pingresp(mosq);
		case PUBACK:
		case PUBCOMP:
			return _mosquitto_handle_pubackcomp(mosq);
		case PUBLISH:
			return _mosquitto_handle_publish(mosq);
		case PUBREC:
			return _mosquitto_handle_pubrec(mosq);
		case PUBREL:
			return _mosquitto_handle_pubrel(mosq);
		case CONNACK:
			return _mosquitto_handle_connack(mosq);
		case SUBACK:
			return _mosquitto_handle_suback(mosq);
		case UNSUBACK:
			return _mosquitto_handle_unsuback(mosq);
		default:
			/* If we don't recognise the command, return an error straight away. */
			fprintf(stderr, "Error: Unrecognised command %d\n", (mosq->in_packet.command)&0xF0);
			return 1;
	}
}

int _mosquitto_handle_pingreq(struct mosquitto *mosq)
{
	if(!mosq || mosq->in_packet.remaining_length != 0){
		return 1;
	}
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PINGREQ");
	return _mosquitto_send_pingresp(mosq);
}

int _mosquitto_handle_pingresp(struct mosquitto *mosq)
{
	if(!mosq || mosq->in_packet.remaining_length != 0){
		return 1;
	}
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PINGRESP");
	return 0;
}

int _mosquitto_handle_pubackcomp(struct mosquitto *mosq)
{
	uint16_t mid;

	if(!mosq || mosq->in_packet.remaining_length != 2){
		return 1;
	}
	if(_mosquitto_read_uint16(&mosq->in_packet, &mid)) return 1;
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBACK (Mid: %d)", mid);

	if(!_mosquitto_message_delete(mosq, mid, mosq_md_out)){
		/* Only inform the client the message has been sent once. */
		if(mosq->on_publish){
			mosq->on_publish(mosq->obj, mid);
		}
	}

	return 0;
}

int _mosquitto_handle_publish(struct mosquitto *mosq)
{
	uint8_t header;
	struct mosquitto_message *message;
	int rc = 0;

	if(!mosq) return 1;

	message = calloc(1, sizeof(struct mosquitto_message));
	if(!message) return 1;

	header = mosq->in_packet.command;

	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBLISH");
	message->direction = mosq_md_in;
	message->dup = (header & 0x08)>>3;
	message->qos = (header & 0x06)>>1;
	message->retain = (header & 0x01);

	if(_mosquitto_read_string(&mosq->in_packet, &message->topic)) return 1;
	if(_mosquitto_fix_sub_topic(&message->topic)) return 1;
	if(!strlen(message->topic)){
		mosquitto_message_cleanup(&message);
		return 1;
	}

	if(message->qos > 0){
		if(_mosquitto_read_uint16(&mosq->in_packet, &message->mid)){
			mosquitto_message_cleanup(&message);
			return 1;
		}
	}

	message->payloadlen = mosq->in_packet.remaining_length - mosq->in_packet.pos;
	if(message->payloadlen){
		message->payload = calloc(message->payloadlen+1, sizeof(uint8_t));
		if(_mosquitto_read_bytes(&mosq->in_packet, message->payload, message->payloadlen)){
			mosquitto_message_cleanup(&message);
			return 1;
		}
	}

	message->timestamp = time(NULL);
	switch(message->qos){
		case 0:
			if(mosq->on_message){
				mosq->on_message(mosq->obj, message);
			}else{
				mosquitto_message_cleanup(&message);
			}
			break;
		case 1:
			if(_mosquitto_send_puback(mosq, message->mid)) rc = 1;
			if(mosq->on_message){
				mosq->on_message(mosq->obj, message);
			}else{
				mosquitto_message_cleanup(&message);
			}
			break;
		case 2:
			if(_mosquitto_send_pubrec(mosq, message->mid)) rc = 1;
			message->state = mosq_ms_wait_pubrel;
			_mosquitto_message_queue(mosq, message);
			break;
	}

	return rc;
}

int _mosquitto_handle_pubrec(struct mosquitto *mosq)
{
	uint16_t mid;

	if(!mosq || mosq->in_packet.remaining_length != 2){
		return 1;
	}
	if(_mosquitto_read_uint16(&mosq->in_packet, &mid)) return 1;
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBREC (Mid: %d)", mid);

	if(_mosquitto_message_update(mosq, mid, mosq_md_out, mosq_ms_wait_pubcomp)) return 1;
	if(_mosquitto_send_pubrel(mosq, mid)) return 1;

	return 0;
}

int _mosquitto_handle_pubrel(struct mosquitto *mosq)
{
	uint16_t mid;
	struct mosquitto_message *message = NULL;

	if(!mosq || mosq->in_packet.remaining_length != 2){
		return 1;
	}
	if(_mosquitto_read_uint16(&mosq->in_packet, &mid)) return 1;
	_mosquitto_log_printf(mosq, MOSQ_LOG_DEBUG, "Received PUBREL (Mid: %d)", mid);

	if(!_mosquitto_message_remove(mosq, mid, mosq_md_in, &message)){
		/* Only pass the message on if we have removed it from the queue - this
		 * prevents multiple callbacks for the same message. */
		if(mosq->on_message){
			mosq->on_message(mosq->obj, message);
		}else{
			mosquitto_message_cleanup(&message);
		}
	}
	if(_mosquitto_send_pubcomp(mosq, mid)) return 1;

	return 0;
}