/*
Copyright (c) 2009-2011 Roger Light <roger@atchoo.org>
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

#include <string.h>

#ifndef CMAKE
#include <config.h>
#endif

#include <mqtt3.h>
#include <mqtt3_protocol.h>
#include <memory_mosq.h>
#include <util_mosq.h>

int mqtt3_raw_disconnect(mqtt3_context *context)
{
	return mqtt3_send_simple_command(context, DISCONNECT);
}

int mqtt3_raw_unsubscribe(mqtt3_context *context, bool dup, const char *topic)
{
	/* FIXME - only deals with a single topic */
	struct _mosquitto_packet *packet = NULL;
	uint32_t packetlen;
	uint16_t mid;
	int rc;

	if(!context || !topic) return MOSQ_ERR_INVAL;

	packet = _mosquitto_calloc(1, sizeof(struct _mosquitto_packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packetlen = 2 + 2+strlen(topic);

	packet->command = SUBSCRIBE | (dup<<3) | (1<<1);
	packet->remaining_length = packetlen;
	rc = _mosquitto_packet_alloc(packet);
	if(rc){
		_mosquitto_free(packet);
		return rc;
	}

	/* Variable header */
	mid = _mosquitto_mid_generate(&context->core);
	_mosquitto_write_uint16(packet, mid);

	/* Payload */
	_mosquitto_write_string(packet, topic, strlen(topic));

	_mosquitto_packet_queue(&context->core, packet);
	return MOSQ_ERR_SUCCESS;
}

