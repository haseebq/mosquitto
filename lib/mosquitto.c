/*
Copyright (c) 2010, Roger Light <roger@atchoo.org>
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

#include <mosquitto.h>
#include <messages_mosq.h>
#include <mqtt3_protocol.h>
#include <net_mosq.h>
#include <read_handle.h>
#include <send_mosq.h>
#include <util_mosq.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct mosquitto *mosquitto_new(void *obj, const char *id)
{
	struct mosquitto *mosq = NULL;

	if(!id) return NULL;

	mosq = (struct mosquitto *)calloc(1, sizeof(struct mosquitto));
	if(mosq){
		mosq->obj = obj;
		mosq->sock = -1;
		mosq->keepalive = 60;
		mosq->message_retry = 20;
		mosq->id = strdup(id);
		mosq->in_packet.payload = NULL;
		_mosquitto_packet_cleanup(&mosq->in_packet);
		mosq->out_packet = NULL;
		mosq->last_msg_in = time(NULL);
		mosq->last_msg_out = time(NULL);
		mosq->last_mid = 0;
		mosq->connected = false;
		mosq->messages = NULL;
		mosq->will = NULL;
		mosq->on_connect = NULL;
		mosq->on_publish = NULL;
		mosq->on_message = NULL;
		mosq->on_subscribe = NULL;
		mosq->on_unsubscribe = NULL;
		mosq->log_destinations = MOSQ_LOG_NONE;
		mosq->log_priorities = MOSQ_LOG_ERR | MOSQ_LOG_WARNING | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO;
	}
	return mosq;
}

int mosquitto_will_set(struct mosquitto *mosq, bool will, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	if(!mosq) return 1;
	if(will && !topic) return 1;

	if(mosq->will){
		if(mosq->will->topic) free(mosq->will->topic);
		if(mosq->will->payload){
			free(mosq->will->payload);
			mosq->will->payload = NULL;
		}
		free(mosq->will);
		mosq->will = NULL;
	}

	if(will){
		mosq->will = calloc(1, sizeof(struct mosquitto_message));
		if(!mosq->will) return 1;
		mosq->will->topic = strdup(topic);
		if(!mosq->will->topic) return 1;
		mosq->will->payloadlen = payloadlen;
		if(mosq->will->payloadlen > 0){
			if(!payload) return 1;
			mosq->will->payload = malloc(sizeof(uint8_t)*mosq->will->payloadlen);
			if(!mosq->will->payload) return 1;

			memcpy(mosq->will->payload, payload, payloadlen);
		}
		mosq->will->qos = qos;
		mosq->will->retain = retain;
	}

	return 0;
}

void mosquitto_destroy(struct mosquitto *mosq)
{
	if(mosq->id) free(mosq->id);
	_mosquitto_message_cleanup_all(mosq);
	free(mosq);
}

int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session)
{
	if(!mosq || !host || !port) return 1;

	mosq->sock = _mosquitto_socket_connect(host, port);

	if(mosq->sock == -1){
		return 1;
	}

	return _mosquitto_send_connect(mosq, keepalive, clean_session);
}

int mosquitto_disconnect(struct mosquitto *mosq)
{
	if(!mosq || mosq->sock < 0) return 1;

	return _mosquitto_send_disconnect(mosq);
}

int mosquitto_publish(struct mosquitto *mosq, uint16_t *mid, const char *topic, uint32_t payloadlen, const uint8_t *payload, int qos, bool retain)
{
	struct mosquitto_message *message;
	uint16_t local_mid;

	if(!mosq || !topic || qos<0 || qos>2) return 1;

	local_mid = _mosquitto_mid_generate(mosq);
	if(mid){
		*mid = local_mid;
	}

	if(qos == 0){
		return _mosquitto_send_publish(mosq, local_mid, topic, payloadlen, payload, qos, retain, false);
	}else{
		message = calloc(1, sizeof(struct mosquitto_message));
		if(!message) return 1;

		message->next = NULL;
		message->timestamp = time(NULL);
		message->direction = mosq_md_out;
		if(qos == 1){
			message->state = mosq_ms_wait_puback;
		}else if(qos == 2){
			message->state = mosq_ms_wait_pubrec;
		}
		message->mid = local_mid;
		message->topic = strdup(topic);
		if(!message->topic){
			mosquitto_message_cleanup(&message);
			return 1;
		}
		if(payloadlen){
			message->payloadlen = payloadlen;
			message->payload = malloc(payloadlen*sizeof(uint8_t));
			if(!message){
				mosquitto_message_cleanup(&message);
				return 1;
			}
		}else{
			message->payloadlen = 0;
			message->payload = NULL;
		}
		message->qos = qos;
		message->retain = retain;
		message->dup = false;

		if(_mosquitto_message_queue(mosq, message)){
			mosquitto_message_cleanup(&message);
			return 1;
		}
		return _mosquitto_send_publish(mosq, message->mid, message->topic, message->payloadlen, message->payload, message->qos, message->retain, message->dup);
	}
}

int mosquitto_subscribe(struct mosquitto *mosq, const char *sub, int qos)
{
	if(!mosq || mosq->sock < 0) return 1;

	return _mosquitto_send_subscribe(mosq, false, sub, qos);
}

int mosquitto_unsubscribe(struct mosquitto *mosq, const char *sub)
{
	if(!mosq || mosq->sock < 0) return 1;

	return _mosquitto_send_unsubscribe(mosq, false, sub);
}

int mosquitto_loop(struct mosquitto *mosq, struct timespec *timeout)
{
	struct timespec local_timeout;
	struct timespec *actual_timeout;
	fd_set readfds, writefds;
	int fdcount;

	if(!mosq || mosq->sock < 0) return 1;

	FD_ZERO(&readfds);
	FD_SET(mosq->sock, &readfds);
	FD_ZERO(&writefds);
	if(mosq->out_packet){
		FD_SET(mosq->sock, &writefds);
	}
	if(timeout){
		actual_timeout = timeout;
	}else{
		local_timeout.tv_sec = 1;
		local_timeout.tv_nsec = 0;
		actual_timeout = &local_timeout;
	}

	fdcount = pselect(mosq->sock+1, &readfds, &writefds, NULL, actual_timeout, NULL);
	if(fdcount == -1){
		fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
		return 1;
	}else{
		if(FD_ISSET(mosq->sock, &readfds)){
			if(mosquitto_read(mosq)){
				fprintf(stderr, "Error in network read.\n");
				_mosquitto_socket_close(mosq);
				return 1;
			}
		}
		if(FD_ISSET(mosq->sock, &writefds)){
			if(mosquitto_write(mosq)){
				fprintf(stderr, "Error in network write.\n");
				_mosquitto_socket_close(mosq);
				return 1;
			}
		}
	}
	_mosquitto_check_keepalive(mosq);

	return 0;
}

int mosquitto_read(struct mosquitto *mosq)
{
	uint8_t byte;
	ssize_t read_length;
	int rc = 0;

	if(!mosq || mosq->sock == -1) return 1;
	/* This gets called if pselect() indicates that there is network data
	 * available - ie. at least one byte.  What we do depends on what data we
	 * already have.
	 * If we've not got a command, attempt to read one and save it. This should
	 * always work because it's only a single byte.
	 * Then try to read the remaining length. This may fail because it is may
	 * be more than one byte - will need to save data pending next read if it
	 * does fail.
	 * Then try to read the remaining payload, where 'payload' here means the
	 * combined variable header and actual payload. This is the most likely to
	 * fail due to longer length, so save current data and current position.
	 * After all data is read, send to _mosquitto_handle_packet() to deal with.
	 * Finally, free the memory and reset everything to starting conditions.
	 */
	if(!mosq->in_packet.command){
		/* FIXME - check command and fill in expected length if we know it.
		 * This means we can check the client is sending valid data some times.
		 */
		read_length = read(mosq->sock, &byte, 1);
		if(read_length == 1){
			mosq->in_packet.command = byte;
		}else{
			if(read_length == 0) return 1; /* EOF */
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}else{
				return 1;
			}
		}
	}
	if(!mosq->in_packet.have_remaining){
		/* Read remaining
		 * Algorithm for decoding taken from pseudo code at
		 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
		 */
		do{
			read_length = read(mosq->sock, &byte, 1);
			if(read_length == 1){
				mosq->in_packet.remaining_count++;
				/* Max 4 bytes length for remaining length as defined by protocol.
				 * Anything more likely means a broken/malicious client.
				 */
				if(mosq->in_packet.remaining_count > 4) return 1;

				mosq->in_packet.remaining_length += (byte & 127) * mosq->in_packet.remaining_mult;
				mosq->in_packet.remaining_mult *= 128;
			}else{
				if(read_length == 0) return 1; /* EOF */
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
				}else{
					return 1;
				}
			}
		}while((byte & 128) != 0);

		if(mosq->in_packet.remaining_length > 0){
			mosq->in_packet.payload = malloc(mosq->in_packet.remaining_length*sizeof(uint8_t));
			if(!mosq->in_packet.payload) return 1;
			mosq->in_packet.to_process = mosq->in_packet.remaining_length;
		}
		mosq->in_packet.have_remaining = 1;
	}
	while(mosq->in_packet.to_process>0){
		read_length = read(mosq->sock, &(mosq->in_packet.payload[mosq->in_packet.pos]), mosq->in_packet.to_process);
		if(read_length > 0){
			mosq->in_packet.to_process -= read_length;
			mosq->in_packet.pos += read_length;
		}else{
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				return 0;
			}else{
				return 1;
			}
		}
	}

	/* All data for this packet is read. */
	mosq->in_packet.pos = 0;
	rc = _mosquitto_packet_handle(mosq);

	/* Free data and reset values */
	_mosquitto_packet_cleanup(&mosq->in_packet);

	mosq->last_msg_in = time(NULL);
	return rc;
}

int mosquitto_write(struct mosquitto *mosq)
{
	uint8_t byte;
	ssize_t write_length;
	struct _mosquitto_packet *packet;

	if(!mosq || mosq->sock == -1) return 1;

	while(mosq->out_packet){
		packet = mosq->out_packet;

		if(packet->command){
			/* Assign to_proces here before remaining_length changes. */
			packet->to_process = packet->remaining_length;
			packet->pos = 0;

			write_length = write(mosq->sock, &packet->command, 1);
			if(write_length == 1){
				packet->command = 0;
			}else{
				if(write_length == 0) return 1; /* EOF */
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
				}else{
					return 1;
				}
			}
		}
		if(!packet->have_remaining){
			/* Write remaining
			 * Algorithm for encoding taken from pseudo code at
			 * http://publib.boulder.ibm.com/infocenter/wmbhelp/v6r0m0/topic/com.ibm.etools.mft.doc/ac10870_.htm
			 */
			do{
				byte = packet->remaining_length % 128;
				packet->remaining_length = packet->remaining_length / 128;
				/* If there are more digits to encode, set the top bit of this digit */
				if(packet->remaining_length>0){
					byte = byte | 0x80;
				}
				write_length = write(mosq->sock, &byte, 1);
				if(write_length == 1){
					packet->remaining_count++;
					/* Max 4 bytes length for remaining length as defined by protocol. */
					if(packet->remaining_count > 4) return 1;
	
				}else{
					if(write_length == 0) return 1; /* EOF */
					if(errno == EAGAIN || errno == EWOULDBLOCK){
						return 0;
					}else{
						return 1;
					}
				}
			}while(packet->remaining_length > 0);
			packet->have_remaining = 1;
		}
		while(packet->to_process > 0){
			write_length = write(mosq->sock, &(packet->payload[packet->pos]), packet->to_process);
			if(write_length > 0){
				packet->to_process -= write_length;
				packet->pos += write_length;
			}else{
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return 0;
				}else{
					return 1;
				}
			}
		}

		if(((packet->command_saved)&0xF6) == PUBLISH && mosq->on_publish){
			/* This is a QoS=0 message */
			mosq->on_publish(mosq->obj, packet->mid);
		}

		/* Free data and reset values */
		mosq->out_packet = packet->next;
		_mosquitto_packet_cleanup(packet);
		free(packet);

		mosq->last_msg_out = time(NULL);
	}
	return 0;
}

void mosquitto_connect_callback_set(struct mosquitto *mosq, void (*on_connect)(void *, int))
{
	if(mosq) mosq->on_connect = on_connect;
}

void mosquitto_publish_callback_set(struct mosquitto *mosq, void (*on_publish)(void *, uint16_t))
{
	if(mosq) mosq->on_publish = on_publish;
}

void mosquitto_message_callback_set(struct mosquitto *mosq, void (*on_message)(void *, struct mosquitto_message *))
{
	if(mosq) mosq->on_message = on_message;
}

void mosquitto_subscribe_callback_set(struct mosquitto *mosq, void (*on_subscribe)(void *, uint16_t, int, uint8_t *))
{
	if(mosq) mosq->on_subscribe = on_subscribe;
}

void mosquitto_unsubscribe_callback_set(struct mosquitto *mosq, void (*on_unsubscribe)(void *, uint16_t))
{
	if(mosq) mosq->on_unsubscribe = on_unsubscribe;
}
