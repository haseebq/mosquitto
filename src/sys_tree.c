/*
Copyright (c) 2009-2012 Roger Light <roger@atchoo.org>
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

#include <math.h>

#include <config.h>

#include <mosquitto_broker.h>

uint64_t g_bytes_received = 0;
uint64_t g_bytes_sent = 0;
uint64_t g_pub_bytes_received = 0;
uint64_t g_pub_bytes_sent = 0;
unsigned long g_msgs_received = 0;
unsigned long g_msgs_sent = 0;
unsigned long g_pub_msgs_received = 0;
unsigned long g_pub_msgs_sent = 0;
unsigned long g_msgs_dropped = 0;
int g_clients_expired = 0;
unsigned int g_socket_connections = 0;
unsigned int g_connection_count = 0;

/* Send messages for the $SYS hierarchy if the last update is longer than
 * 'interval' seconds ago.
 * 'interval' is the amount of seconds between updates. If 0, then no periodic
 * messages are sent for the $SYS hierarchy.
 * 'start_time' is the result of time() that the broker was started at.
 */
void mqtt3_db_sys_update(struct mosquitto_db *db, int interval, time_t start_time)
{
	static time_t last_update = 0;
	time_t now = time(NULL);
	time_t uptime;
	char buf[100];
	unsigned int value;
	unsigned int inactive;
	unsigned int active;
#ifndef WIN32
	unsigned long value_ul;
#endif

	static int msg_store_count = -1;
	static unsigned int client_count = -1;
	static int clients_expired = -1;
	static unsigned int client_max = -1;
	static unsigned int inactive_count = -1;
	static unsigned int active_count = -1;
#ifdef REAL_WITH_MEMORY_TRACKING
	static unsigned long current_heap = -1;
	static unsigned long max_heap = -1;
#endif
	static unsigned long msgs_received = -1;
	static unsigned long msgs_sent = -1;
	static unsigned long msgs_dropped = -1;
	static unsigned long pub_msgs_received = -1;
	static unsigned long pub_msgs_sent = -1;
	static unsigned long long bytes_received = -1;
	static unsigned long long bytes_sent = -1;
	static unsigned long long pub_bytes_received = -1;
	static unsigned long long pub_bytes_sent = -1;
	static int subscription_count = -1;
	static int retained_count = -1;

	static double msgs_received_load1 = 0;
	static double msgs_received_load5 = 0;
	static double msgs_received_load15 = 0;
	static double msgs_sent_load1 = 0;
	static double msgs_sent_load5 = 0;
	static double msgs_sent_load15 = 0;
	double new_msgs_received_load1, new_msgs_received_load5, new_msgs_received_load15;
	double new_msgs_sent_load1, new_msgs_sent_load5, new_msgs_sent_load15;
	double msgs_received_interval, msgs_sent_interval;

	static double publish_received_load1 = 0;
	static double publish_received_load5 = 0;
	static double publish_received_load15 = 0;
	static double publish_sent_load1 = 0;
	static double publish_sent_load5 = 0;
	static double publish_sent_load15 = 0;
	double new_publish_received_load1, new_publish_received_load5, new_publish_received_load15;
	double new_publish_sent_load1, new_publish_sent_load5, new_publish_sent_load15;
	double publish_received_interval, publish_sent_interval;

	static double bytes_received_load1 = 0;
	static double bytes_received_load5 = 0;
	static double bytes_received_load15 = 0;
	static double bytes_sent_load1 = 0;
	static double bytes_sent_load5 = 0;
	static double bytes_sent_load15 = 0;
	double new_bytes_received_load1, new_bytes_received_load5, new_bytes_received_load15;
	double new_bytes_sent_load1, new_bytes_sent_load5, new_bytes_sent_load15;
	double bytes_received_interval, bytes_sent_interval;

	static double socket_load1 = 0;
	static double socket_load5 = 0;
	static double socket_load15 = 0;
	double new_socket_load1, new_socket_load5, new_socket_load15;
	double socket_interval;

	static double connection_load1 = 0;
	static double connection_load5 = 0;
	static double connection_load15 = 0;
	double new_connection_load1, new_connection_load5, new_connection_load15;
	double connection_interval;

	double exponent;

	if(interval && now - interval > last_update){
		uptime = now - start_time;
		snprintf(buf, 100, "%d seconds", (int)uptime);
		mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/uptime", 2, strlen(buf), buf, 1);

		if(last_update > 0){
			msgs_received_interval = g_msgs_received - msgs_received;
			msgs_sent_interval = g_msgs_sent - msgs_sent;

			publish_received_interval = g_pub_msgs_received - pub_msgs_received;
			publish_sent_interval = g_pub_msgs_sent - pub_msgs_sent;

			bytes_received_interval = g_bytes_received - bytes_received;
			bytes_sent_interval = g_bytes_sent - bytes_sent;

			socket_interval = g_socket_connections;
			g_socket_connections = 0;
			connection_interval = g_connection_count;
			g_connection_count = 0;

			/* 1 minute load */
			exponent = exp(-1.0*(now-last_update)/60.0);

			new_msgs_received_load1 = msgs_received_interval + exponent*(msgs_received_load1 - msgs_received_interval);
			if(fabs(new_msgs_received_load1 - msgs_received_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_msgs_received_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/messages/received/1min", 2, strlen(buf), buf, 1);
			}
			msgs_received_load1 = new_msgs_received_load1;

			new_msgs_sent_load1 = msgs_sent_interval + exponent*(msgs_sent_load1 - msgs_sent_interval);
			if(fabs(new_msgs_sent_load1 - msgs_sent_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_msgs_sent_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/messages/sent/1min", 2, strlen(buf), buf, 1);
			}
			msgs_sent_load1 = new_msgs_sent_load1;


			new_publish_received_load1 = publish_received_interval + exponent*(publish_received_load1 - publish_received_interval);
			if(fabs(new_publish_received_load1 - publish_received_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_publish_received_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/publish/received/1min", 2, strlen(buf), buf, 1);
			}
			publish_received_load1 = new_publish_received_load1;

			new_publish_sent_load1 = publish_sent_interval + exponent*(publish_sent_load1 - publish_sent_interval);
			if(fabs(new_publish_sent_load1 - publish_sent_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_publish_sent_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/publish/sent/1min", 2, strlen(buf), buf, 1);
			}
			publish_sent_load1 = new_publish_sent_load1;

			new_bytes_received_load1 = bytes_received_interval + exponent*(bytes_received_load1 - bytes_received_interval);
			if(fabs(new_bytes_received_load1 - bytes_received_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_bytes_received_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/bytes/received/1min", 2, strlen(buf), buf, 1);
			}
			bytes_received_load1 = new_bytes_received_load1;

			new_bytes_sent_load1 = bytes_sent_interval + exponent*(bytes_sent_load1 - bytes_sent_interval);
			if(fabs(new_bytes_sent_load1 - bytes_sent_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_bytes_sent_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/bytes/sent/1min", 2, strlen(buf), buf, 1);
			}
			bytes_sent_load1 = new_bytes_sent_load1;

			new_socket_load1 = socket_interval + exponent*(socket_load1 - socket_interval);
			if(fabs(new_socket_load1 - socket_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_socket_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/sockets/1min", 2, strlen(buf), buf, 1);
			}
			socket_load1 = new_socket_load1;

			new_connection_load1 = connection_interval + exponent*(connection_load1 - connection_interval);
			if(fabs(new_connection_load1 - connection_load1) >= 0.01){
				snprintf(buf, 100, "%.2f", new_connection_load1);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/connections/1min", 2, strlen(buf), buf, 1);
			}
			connection_load1 = new_connection_load1;

			/* 5 minute load */
			exponent = exp(-1.0*(now-last_update)/300.0);

			new_msgs_received_load5 = msgs_received_interval + exponent*(msgs_received_load5 - msgs_received_interval);
			if(fabs(new_msgs_received_load5 - msgs_received_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_msgs_received_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/messages/received/5min", 2, strlen(buf), buf, 1);
			}
			msgs_received_load5 = new_msgs_received_load5;

			new_msgs_sent_load5 = msgs_sent_interval + exponent*(msgs_sent_load5 - msgs_sent_interval);
			if(fabs(new_msgs_sent_load5 - msgs_sent_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_msgs_sent_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/messages/sent/5min", 2, strlen(buf), buf, 1);
			}
			msgs_sent_load5 = new_msgs_sent_load5;


			new_publish_received_load5 = publish_received_interval + exponent*(publish_received_load5 - publish_received_interval);
			if(fabs(new_publish_received_load5 - publish_received_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_publish_received_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/publish/received/5min", 2, strlen(buf), buf, 1);
			}
			publish_received_load5 = new_publish_received_load5;

			new_publish_sent_load5 = publish_sent_interval + exponent*(publish_sent_load5 - publish_sent_interval);
			if(fabs(new_publish_sent_load5 - publish_sent_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_publish_sent_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/publish/sent/5min", 2, strlen(buf), buf, 1);
			}
			publish_sent_load5 = new_publish_sent_load5;


			new_bytes_received_load5 = bytes_received_interval + exponent*(bytes_received_load5 - bytes_received_interval);
			if(fabs(new_bytes_received_load5 - bytes_received_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_bytes_received_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/bytes/received/5min", 2, strlen(buf), buf, 1);
			}
			bytes_received_load5 = new_bytes_received_load5;

			new_bytes_sent_load5 = bytes_sent_interval + exponent*(bytes_sent_load5 - bytes_sent_interval);
			if(fabs(new_bytes_sent_load5 - bytes_sent_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_bytes_sent_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/bytes/sent/5min", 2, strlen(buf), buf, 1);
			}
			bytes_sent_load5 = new_bytes_sent_load5;

			new_socket_load5 = socket_interval + exponent*(socket_load5 - socket_interval);
			if(fabs(new_socket_load5 - socket_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_socket_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/sockets/5min", 2, strlen(buf), buf, 1);
			}
			socket_load5 = new_socket_load5;

			new_connection_load5 = connection_interval + exponent*(connection_load5 - connection_interval);
			if(fabs(new_connection_load5 - connection_load5) >= 0.01){
				snprintf(buf, 100, "%.2f", new_connection_load5);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/connections/5min", 2, strlen(buf), buf, 1);
			}
			connection_load5 = new_connection_load5;

			/* 15 minute load */
			exponent = exp(-1.0*(now-last_update)/900.0);

			new_msgs_received_load15 = msgs_received_interval + exponent*(msgs_received_load15 - msgs_received_interval);
			if(fabs(new_msgs_received_load15 - msgs_received_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_msgs_received_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/messages/received/15min", 2, strlen(buf), buf, 1);
			}
			msgs_received_load15 = new_msgs_received_load15;

			new_msgs_sent_load15 = msgs_sent_interval + exponent*(msgs_sent_load15 - msgs_sent_interval);
			if(fabs(new_msgs_sent_load15 - msgs_sent_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_msgs_sent_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/messages/sent/15min", 2, strlen(buf), buf, 1);
			}
			msgs_sent_load15 = new_msgs_sent_load15;


			new_publish_received_load15 = publish_received_interval + exponent*(publish_received_load15 - publish_received_interval);
			if(fabs(new_publish_received_load15 - publish_received_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_publish_received_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/publish/received/15min", 2, strlen(buf), buf, 1);
			}
			publish_received_load15 = new_publish_received_load15;

			new_publish_sent_load15 = publish_sent_interval + exponent*(publish_sent_load15 - publish_sent_interval);
			if(fabs(new_publish_sent_load15 - publish_sent_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_publish_sent_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/publish/sent/15min", 2, strlen(buf), buf, 1);
			}
			publish_sent_load15 = new_publish_sent_load15;


			new_bytes_received_load15 = bytes_received_interval + exponent*(bytes_received_load15 - bytes_received_interval);
			if(fabs(new_bytes_received_load15 - bytes_received_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_bytes_received_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/bytes/received/15min", 2, strlen(buf), buf, 1);
			}
			bytes_received_load15 = new_bytes_received_load15;

			new_bytes_sent_load15 = bytes_sent_interval + exponent*(bytes_sent_load15 - bytes_sent_interval);
			if(fabs(new_bytes_sent_load15 - bytes_sent_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_bytes_sent_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/bytes/sent/15min", 2, strlen(buf), buf, 1);
			}
			bytes_sent_load15 = new_bytes_sent_load15;

			new_socket_load15 = socket_interval + exponent*(socket_load15 - socket_interval);
			if(fabs(new_socket_load15 - socket_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_socket_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/sockets/15min", 2, strlen(buf), buf, 1);
			}
			socket_load15 = new_socket_load15;

			new_connection_load15 = connection_interval + exponent*(connection_load15 - connection_interval);
			if(fabs(new_connection_load15 - connection_load15) >= 0.01){
				snprintf(buf, 100, "%.2f", new_connection_load15);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/load/connections/15min", 2, strlen(buf), buf, 1);
			}
			connection_load15 = new_connection_load15;
		}

		if(db->msg_store_count != msg_store_count){
			msg_store_count = db->msg_store_count;
			snprintf(buf, 100, "%d", msg_store_count);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/stored", 2, strlen(buf), buf, 1);
		}

		if(db->subscription_count != subscription_count){
			subscription_count = db->subscription_count;
			snprintf(buf, 100, "%d", subscription_count);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/subscriptions/count", 2, strlen(buf), buf, 1);
		}

		if(db->retained_count != retained_count){
			retained_count = db->retained_count;
			snprintf(buf, 100, "%d", retained_count);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/retained messages/count", 2, strlen(buf), buf, 1);
		}

		if(!mqtt3_db_client_count(db, &value, &inactive)){
			if(client_count != value){
				client_count = value;
				snprintf(buf, 100, "%d", client_count);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/clients/total", 2, strlen(buf), buf, 1);
			}
			if(inactive_count != inactive){
				inactive_count = inactive;
				snprintf(buf, 100, "%d", inactive_count);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/clients/inactive", 2, strlen(buf), buf, 1);
			}
			active = client_count - inactive;
			if(active_count != active){
				active_count = active;
				snprintf(buf, 100, "%d", active_count);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/clients/active", 2, strlen(buf), buf, 1);
			}
			if(value != client_max){
				client_max = value;
				snprintf(buf, 100, "%d", client_max);
				mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/clients/maximum", 2, strlen(buf), buf, 1);
			}
		}
		if(g_clients_expired != clients_expired){
			clients_expired = g_clients_expired;
			snprintf(buf, 100, "%d", clients_expired);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/clients/expired", 2, strlen(buf), buf, 1);
		}

#ifdef REAL_WITH_MEMORY_TRACKING
		value_ul = _mosquitto_memory_used();
		if(current_heap != value_ul){
			current_heap = value_ul;
			snprintf(buf, 100, "%lu", current_heap);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/heap/current size", 2, strlen(buf), buf, 1);
		}
		value_ul =_mosquitto_max_memory_used();
		if(max_heap != value_ul){
			max_heap = value_ul;
			snprintf(buf, 100, "%lu", max_heap);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/heap/maximum size", 2, strlen(buf), buf, 1);
		}
#endif

		if(msgs_received != g_msgs_received){
			msgs_received = g_msgs_received;
			snprintf(buf, 100, "%lu", msgs_received);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/received", 2, strlen(buf), buf, 1);
		}
		
		if(msgs_sent != g_msgs_sent){
			msgs_sent = g_msgs_sent;
			snprintf(buf, 100, "%lu", msgs_sent);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/sent", 2, strlen(buf), buf, 1);
		}

		if(msgs_dropped != g_msgs_dropped){
			msgs_dropped = g_msgs_dropped;
			snprintf(buf, 100, "%lu", msgs_dropped);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/messages/dropped", 2, strlen(buf), buf, 1);
		}

		if(pub_msgs_received != g_pub_msgs_received){
			pub_msgs_received = g_pub_msgs_received;
			snprintf(buf, 100, "%lu", pub_msgs_received);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/publish/messages/received", 2, strlen(buf), buf, 1);
		}
		
		if(pub_msgs_sent != g_pub_msgs_sent){
			pub_msgs_sent = g_pub_msgs_sent;
			snprintf(buf, 100, "%lu", pub_msgs_sent);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/publish/messages/sent", 2, strlen(buf), buf, 1);
		}

		if(bytes_received != g_bytes_received){
			bytes_received = g_bytes_received;
			snprintf(buf, 100, "%llu", bytes_received);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/bytes/received", 2, strlen(buf), buf, 1);
		}
		
		if(bytes_sent != g_bytes_sent){
			bytes_sent = g_bytes_sent;
			snprintf(buf, 100, "%llu", bytes_sent);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/bytes/sent", 2, strlen(buf), buf, 1);
		}
		
		if(pub_bytes_received != g_pub_bytes_received){
			pub_bytes_received = g_pub_bytes_received;
			snprintf(buf, 100, "%llu", pub_bytes_received);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/publish/bytes/received", 2, strlen(buf), buf, 1);
		}

		if(pub_bytes_sent != g_pub_bytes_sent){
			pub_bytes_sent = g_pub_bytes_sent;
			snprintf(buf, 100, "%llu", pub_bytes_sent);
			mqtt3_db_messages_easy_queue(db, NULL, "$SYS/broker/publish/bytes/sent", 2, strlen(buf), buf, 1);
		}

		last_update = time(NULL);
	}
}

