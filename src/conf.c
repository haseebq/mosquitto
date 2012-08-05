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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#else
#  include <dirent.h>
#endif

#include <mosquitto_broker.h>
#include <memory_mosq.h>
#include "util_mosq.h"

struct config_recurse {
	int log_dest;
	int log_dest_set;
	int log_type;
	int log_type_set;
	int max_inflight_messages;
	int max_queued_messages;
};

static int _conf_parse_bool(char **token, const char *name, bool *value, char *saveptr);
static int _conf_parse_int(char **token, const char *name, int *value, char *saveptr);
static int _conf_parse_string(char **token, const char *name, char **value, char *saveptr);
static int _config_read_file(mqtt3_config *config, bool reload, const char *file, struct config_recurse *config_tmp, int level);

static void _config_init_reload(mqtt3_config *config)
{
	int i;
	/* Set defaults */
	if(config->acl_file) _mosquitto_free(config->acl_file);
	config->acl_file = NULL;
	config->allow_anonymous = true;
	config->autosave_interval = 1800;
	config->autosave_on_changes = false;
	if(config->clientid_prefixes) _mosquitto_free(config->clientid_prefixes);
	config->connection_messages = true;
	config->clientid_prefixes = NULL;
#ifndef WIN32
	config->log_dest = MQTT3_LOG_STDERR;
	config->log_type = MOSQ_LOG_ERR | MOSQ_LOG_WARNING | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO;
#else
	config->log_dest = MQTT3_LOG_SYSLOG;
	config->log_type = MOSQ_LOG_ERR | MOSQ_LOG_WARNING;
#endif
	config->log_timestamp = true;
	if(config->password_file) _mosquitto_free(config->password_file);
	config->password_file = NULL;
	config->persistence = false;
	if(config->persistence_location) _mosquitto_free(config->persistence_location);
	config->persistence_location = NULL;
	if(config->persistence_file) _mosquitto_free(config->persistence_file);
	config->persistence_file = NULL;
	config->persistent_client_expiration = 0;
	if(config->psk_file) _mosquitto_free(config->psk_file);
	config->psk_file = NULL;
	config->queue_qos0_messages = false;
	config->retry_interval = 20;
	config->store_clean_interval = 10;
	config->sys_interval = 10;
	if(config->auth_options){
		for(i=0; i<config->auth_option_count; i++){
			_mosquitto_free(config->auth_options[i].key);
			_mosquitto_free(config->auth_options[i].value);
		}
		_mosquitto_free(config->auth_options);
		config->auth_options = NULL;
		config->auth_option_count = 0;
	}
}

void mqtt3_config_init(mqtt3_config *config)
{
	memset(config, 0, sizeof(mqtt3_config));
	_config_init_reload(config);
	config->config_file = NULL;
	config->daemon = false;
	config->default_listener.host = NULL;
	config->default_listener.port = 0;
	config->default_listener.max_connections = -1;
	config->default_listener.mount_point = NULL;
	config->default_listener.socks = NULL;
	config->default_listener.sock_count = 0;
	config->default_listener.client_count = 0;
#ifdef WITH_TLS
	config->default_listener.cafile = NULL;
	config->default_listener.capath = NULL;
	config->default_listener.certfile = NULL;
	config->default_listener.keyfile = NULL;
	config->default_listener.ciphers = NULL;
	config->default_listener.psk_hint = NULL;
	config->default_listener.require_certificate = false;
	config->default_listener.crlfile = NULL;
	config->default_listener.use_identity_as_username = false;
#endif
	config->listeners = NULL;
	config->listener_count = 0;
	config->pid_file = NULL;
	config->user = NULL;
#ifdef WITH_BRIDGE
	config->bridges = NULL;
	config->bridge_count = 0;
#endif
	config->auth_plugin = NULL;
}

void mqtt3_config_cleanup(mqtt3_config *config)
{
	int i, j;

	if(config->acl_file) _mosquitto_free(config->acl_file);
	if(config->clientid_prefixes) _mosquitto_free(config->clientid_prefixes);
	if(config->config_file) _mosquitto_free(config->config_file);
	if(config->password_file) _mosquitto_free(config->password_file);
	if(config->psk_file) _mosquitto_free(config->psk_file);
	if(config->persistence_location) _mosquitto_free(config->persistence_location);
	if(config->persistence_file) _mosquitto_free(config->persistence_file);
	if(config->persistence_filepath) _mosquitto_free(config->persistence_filepath);
	if(config->psk_file) _mosquitto_free(config->psk_file);
	if(config->listeners){
		for(i=0; i<config->listener_count; i++){
			if(config->listeners[i].host) _mosquitto_free(config->listeners[i].host);
			if(config->listeners[i].mount_point) _mosquitto_free(config->listeners[i].mount_point);
			if(config->listeners[i].socks) _mosquitto_free(config->listeners[i].socks);
		}
		_mosquitto_free(config->listeners);
	}
#ifdef WITH_BRIDGE
	if(config->bridges){
		for(i=0; i<config->bridge_count; i++){
			if(config->bridges[i].name) _mosquitto_free(config->bridges[i].name);
			if(config->bridges[i].address) _mosquitto_free(config->bridges[i].address);
			if(config->bridges[i].clientid) _mosquitto_free(config->bridges[i].clientid);
			if(config->bridges[i].username) _mosquitto_free(config->bridges[i].username);
			if(config->bridges[i].password) _mosquitto_free(config->bridges[i].password);
			if(config->bridges[i].topics){
				for(j=0; j<config->bridges[i].topic_count; j++){
					if(config->bridges[i].topics[j].topic) _mosquitto_free(config->bridges[i].topics[j].topic);
					if(config->bridges[i].topics[j].local_prefix) _mosquitto_free(config->bridges[i].topics[j].local_prefix);
					if(config->bridges[i].topics[j].remote_prefix) _mosquitto_free(config->bridges[i].topics[j].remote_prefix);
					if(config->bridges[i].topics[j].local_topic) _mosquitto_free(config->bridges[i].topics[j].local_topic);
					if(config->bridges[i].topics[j].remote_topic) _mosquitto_free(config->bridges[i].topics[j].remote_topic);
				}
				_mosquitto_free(config->bridges[i].topics);
			}
			if(config->bridges[i].notification_topic) _mosquitto_free(config->bridges[i].notification_topic);
		}
		_mosquitto_free(config->bridges);
	}
#endif
	if(config->auth_plugin) _mosquitto_free(config->auth_plugin);
	if(config->auth_options){
		for(i=0; i<config->auth_option_count; i++){
			_mosquitto_free(config->auth_options[i].key);
			_mosquitto_free(config->auth_options[i].value);
		}
		_mosquitto_free(config->auth_options);
		config->auth_options = NULL;
		config->auth_option_count = 0;
	}
}

static void print_usage(void)
{
	printf("mosquitto version %s (build date %s)\n\n", VERSION, TIMESTAMP);
	printf("mosquitto is an MQTT v3.1 broker.\n\n");
	printf("Usage: mosquitto [-c config_file] [-d] [-h] [-p port]\n\n");
	printf(" -c : specify the broker config file.\n");
	printf(" -d : put the broker into the background after starting.\n");
	printf(" -h : display this help.\n");
	printf(" -p : start the broker listening on the specified port.\n");
	printf("      Not recommended in conjunction with the -c option.\n");
	printf("\nSee http://mosquitto.org/ for more information.\n\n");
}

int mqtt3_config_parse_args(mqtt3_config *config, int argc, char *argv[])
{
	int i;
	int port_tmp;

	for(i=1; i<argc; i++){
		if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config-file")){
			if(i<argc-1){
				config->config_file = _mosquitto_strdup(argv[i+1]);
				if(!config->config_file){
					_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
					return MOSQ_ERR_NOMEM;
				}

				if(mqtt3_config_read(config, false)){
					_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Unable to open configuration file.");
					return MOSQ_ERR_INVAL;
				}
			}else{
				_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: -c argument given, but no config file specified.");
				return MOSQ_ERR_INVAL;
			}
			i++;
		}else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--daemon")){
			config->daemon = true;
		}else if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")){
			print_usage();
			return MOSQ_ERR_INVAL;
		}else if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")){
			if(i<argc-1){
				port_tmp = atoi(argv[i+1]);
				if(port_tmp<1 || port_tmp>65535){
					_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid port specified (%d).", port_tmp);
					return MOSQ_ERR_INVAL;
				}else{
					if(config->default_listener.port){
						_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Default listener port specified multiple times. Only the latest will be used.");
					}
					config->default_listener.port = port_tmp;
				}
			}else{
				_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: -p argument given, but no port specified.");
				return MOSQ_ERR_INVAL;
			}
			i++;
		}else{
			fprintf(stderr, "Error: Unknown option '%s'.\n",argv[i]);
			print_usage();
			return MOSQ_ERR_INVAL;
		}
	}

	if(config->listener_count == 0 || config->default_listener.host || config->default_listener.port){
		config->listener_count++;
		config->listeners = _mosquitto_realloc(config->listeners, sizeof(struct _mqtt3_listener)*config->listener_count);
		if(!config->listeners){
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		if(config->default_listener.port){
			config->listeners[config->listener_count-1].port = config->default_listener.port;
		}else{
			config->listeners[config->listener_count-1].port = 1883;
		}
		if(config->default_listener.host){
			config->listeners[config->listener_count-1].host = config->default_listener.host;
		}else{
			config->listeners[config->listener_count-1].host = NULL;
		}
		if(config->default_listener.mount_point){
			config->listeners[config->listener_count-1].mount_point = config->default_listener.host;
		}else{
			config->listeners[config->listener_count-1].mount_point = NULL;
		}
		config->listeners[config->listener_count-1].max_connections = config->default_listener.max_connections;
		config->listeners[config->listener_count-1].client_count = 0;
		config->listeners[config->listener_count-1].socks = NULL;
		config->listeners[config->listener_count-1].sock_count = 0;
		config->listeners[config->listener_count-1].client_count = 0;
#ifdef WITH_TLS
		config->listeners[config->listener_count-1].cafile = config->default_listener.cafile;
		config->listeners[config->listener_count-1].capath = config->default_listener.capath;
		config->listeners[config->listener_count-1].certfile = config->default_listener.certfile;
		config->listeners[config->listener_count-1].keyfile = config->default_listener.keyfile;
		config->listeners[config->listener_count-1].ciphers = config->default_listener.ciphers;
		config->listeners[config->listener_count-1].psk_hint = config->default_listener.psk_hint;
		config->listeners[config->listener_count-1].require_certificate = config->default_listener.require_certificate;
		config->listeners[config->listener_count-1].ssl_ctx = NULL;
		config->listeners[config->listener_count-1].crlfile = config->default_listener.crlfile;
		config->listeners[config->listener_count-1].use_identity_as_username = config->default_listener.use_identity_as_username;
#endif
	}

	/* Default to drop to mosquitto user if we are privileged and no user specified. */
	if(!config->user){
		config->user = "mosquitto";
	}
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_config_read(mqtt3_config *config, bool reload)
{
	int rc = MOSQ_ERR_SUCCESS;
	int max_inflight_messages = 20;
	int max_queued_messages = 100;
	struct config_recurse cr;
	int i;

	cr.log_dest = MQTT3_LOG_NONE;
	cr.log_dest_set = 0;
	cr.log_type = MOSQ_LOG_NONE;
	cr.log_type_set = 0;
	cr.max_inflight_messages = 20;
	cr.max_queued_messages = 100;

	if(!config->config_file) return 0;

	if(reload){
		/* Re-initialise appropriate config vars to default for reload. */
		_config_init_reload(config);
	}
	rc = _config_read_file(config, reload, config->config_file, &cr, 0);
	if(rc) return rc;

#ifdef WITH_PERSISTENCE
	if(config->persistence){
		if(!config->persistence_file){
			config->persistence_file = _mosquitto_strdup("mosquitto.db");
			if(!config->persistence_file) return MOSQ_ERR_NOMEM;
		}
		if(config->persistence_filepath){
			_mosquitto_free(config->persistence_filepath);
		}
		if(config->persistence_location && strlen(config->persistence_location)){
			config->persistence_filepath = _mosquitto_malloc(strlen(config->persistence_location) + strlen(config->persistence_file) + 1);
			if(!config->persistence_filepath) return MOSQ_ERR_NOMEM;
			sprintf(config->persistence_filepath, "%s%s", config->persistence_location, config->persistence_file);
		}else{
			config->persistence_filepath = _mosquitto_strdup(config->persistence_file);
			if(!config->persistence_filepath) return MOSQ_ERR_NOMEM;
		}
	}
#endif
	/* Default to drop to mosquitto user if no other user specified. This must
	 * remain here even though it is covered in mqtt3_parse_args() because this
	 * function may be called on its own. */
	if(!config->user){
		config->user = "mosquitto";
	}

	mqtt3_db_limits_set(max_inflight_messages, max_queued_messages);

#ifdef WITH_BRIDGE
	for(i=0; i<config->bridge_count; i++){
		if(!config->bridges[i].name || !config->bridges[i].address || !config->bridges[i].port || !config->bridges[i].topic_count){
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
			return MOSQ_ERR_INVAL;
		}
	}
#endif

	if(cr.log_dest_set){
		config->log_dest = cr.log_dest;
	}
	if(cr.log_type_set){
		config->log_type = cr.log_type;
	}
	return MOSQ_ERR_SUCCESS;
}

int _config_read_file(mqtt3_config *config, bool reload, const char *file, struct config_recurse *cr, int level)
{
	int rc;
	FILE *fptr = NULL;
	char buf[1024];
	char *token;
	int port_tmp;
	char *saveptr = NULL;
#ifdef WITH_BRIDGE
	struct _mqtt3_bridge *cur_bridge = NULL;
	struct _mqtt3_bridge_topic *cur_topic;
#endif
	time_t expiration_mult;
	char *key;
	char *conf_file;
#ifdef WIN32
	HANDLE fh;
	char dirpath[MAX_PATH];
	WIN32_FIND_DATA find_data;
#else
	DIR *dh;
	struct dirent *de;
#endif
	int len;
	struct _mqtt3_listener *cur_listener = &config->default_listener;
	
	fptr = fopen(file, "rt");
	if(!fptr) return 1;

	while(fgets(buf, 1024, fptr)){
		if(buf[0] != '#' && buf[0] != 10 && buf[0] != 13){
			while(buf[strlen(buf)-1] == 10 || buf[strlen(buf)-1] == 13){
				buf[strlen(buf)-1] = 0;
			}
			token = strtok_r(buf, " ", &saveptr);
			if(token){
				if(!strcmp(token, "acl_file")){
					if(reload){
						if(config->acl_file){
							_mosquitto_free(config->acl_file);
							config->acl_file = NULL;
						}
					}
					if(_conf_parse_string(&token, "acl_file", &config->acl_file, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "address") || !strcmp(token, "addresses")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge || cur_bridge->address){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						token = strtok_r(token, ":", &saveptr);
						if(token){
							cur_bridge->address = _mosquitto_strdup(token);
							if(!cur_bridge->address){
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
								return MOSQ_ERR_NOMEM;
							}
							token = strtok_r(NULL, ":", &saveptr);
							if(token){
								port_tmp = atoi(token);
								if(port_tmp < 1 || port_tmp > 65535){
									_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
									return MOSQ_ERR_INVAL;
								}
								cur_bridge->port = port_tmp;
							}else{
								cur_bridge->port = 1883;
							}
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty address value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "allow_anonymous")){
					if(_conf_parse_bool(&token, "allow_anonymous", &config->allow_anonymous, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strncmp(token, "auth_opt_", 9)){
					if(strlen(token) < 12){
						/* auth_opt_ == 9, + one digit key == 10, + one space == 11, + one value == 12 */
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid auth_opt_ config option.");
						return MOSQ_ERR_INVAL;
					}
					key = _mosquitto_strdup(&token[9]);
					if(!key){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
						return MOSQ_ERR_NOMEM;
					}else if(strlen(key) == 0){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid auth_opt_ config option.");
						return MOSQ_ERR_INVAL;
					}
					token += 9+strlen(key)+1;
					if(token[0]){
						config->auth_option_count++;
						config->auth_options = _mosquitto_realloc(config->auth_options, config->auth_option_count*sizeof(struct mosquitto_auth_opt));
						if(!config->auth_options){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
							return MOSQ_ERR_NOMEM;
						}
						config->auth_options[config->auth_option_count-1].key = key;
						config->auth_options[config->auth_option_count-1].value = _mosquitto_strdup(token);
						if(!config->auth_options[config->auth_option_count-1].value){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty %s value in configuration.", key);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "auth_plugin")){
					if(reload) continue; // Auth plugin not currently valid for reloading.
					if(_conf_parse_string(&token, "auth_plugin", &config->auth_plugin, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "autosave_interval")){
					if(_conf_parse_int(&token, "autosave_interval", &config->autosave_interval, saveptr)) return MOSQ_ERR_INVAL;
					if(config->autosave_interval < 0) config->autosave_interval = 0;
				}else if(!strcmp(token, "autosave_on_changes")){
					if(_conf_parse_bool(&token, "autosave_on_changes", &config->autosave_on_changes, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "bind_address")){
					if(reload) continue; // Listener not valid for reloading.
					if(_conf_parse_string(&token, "default listener bind_address", &config->default_listener.host, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "bridge_cafile")){
#if defined(WITH_BRIDGE) && defined(WITH_TLS)
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->tls_cafile){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate bridge_cafile value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->tls_cafile = _mosquitto_strdup(token);
						if(!cur_bridge->tls_cafile){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty bridge_cafile value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge and/or TLS support not available.");
#endif
				}else if(!strcmp(token, "bridge_capath")){
#if defined(WITH_BRIDGE) && defined(WITH_TLS)
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->tls_capath){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate bridge_capath value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->tls_capath = _mosquitto_strdup(token);
						if(!cur_bridge->tls_capath){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty bridge_capath value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge and/or TLS support not available.");
#endif
				}else if(!strcmp(token, "bridge_certfile")){
#if defined(WITH_BRIDGE) && defined(WITH_TLS)
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->tls_certfile){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate bridge_certfile value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->tls_certfile = _mosquitto_strdup(token);
						if(!cur_bridge->tls_certfile){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty bridge_certfile value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge and/or TLS support not available.");
#endif
				}else if(!strcmp(token, "bridge_keyfile")){
#if defined(WITH_BRIDGE) && defined(WITH_TLS)
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->tls_keyfile){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate bridge_keyfile value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->tls_keyfile = _mosquitto_strdup(token);
						if(!cur_bridge->tls_keyfile){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty bridge_keyfile value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge and/or TLS support not available.");
#endif
				}else if(!strcmp(token, "cafile")){
#if defined(WITH_TLS)
					if(reload) continue; // Listeners not valid for reloading.
					if(cur_listener->psk_hint){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Cannot use both certificate and psk encryption in a single listener.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_string(&token, "cafile", &cur_listener->cafile, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "capath")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_string(&token, "capath", &cur_listener->capath, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "certfile")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(cur_listener->psk_hint){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Cannot use both certificate and psk encryption in a single listener.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_string(&token, "certfile", &cur_listener->certfile, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "ciphers")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_string(&token, "ciphers", &cur_listener->ciphers, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "clientid")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->clientid){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate clientid value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->clientid = _mosquitto_strdup(token);
						if(!cur_bridge->clientid){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty clientid value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "cleansession")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_bool(&token, "cleansession", &cur_bridge->clean_session, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "clientid_prefixes")){
					if(reload){
						if(config->clientid_prefixes){
							_mosquitto_free(config->clientid_prefixes);
							config->clientid_prefixes = NULL;
						}
					}
					if(_conf_parse_string(&token, "clientid_prefixes", &config->clientid_prefixes, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "connection")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						config->bridge_count++;
						config->bridges = _mosquitto_realloc(config->bridges, config->bridge_count*sizeof(struct _mqtt3_bridge));
						if(!config->bridges){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
							return MOSQ_ERR_NOMEM;
						}
						cur_bridge = &(config->bridges[config->bridge_count-1]);
						cur_bridge->name = _mosquitto_strdup(token);
						cur_bridge->address = NULL;
						cur_bridge->keepalive = 60;
						cur_bridge->clean_session = false;
						cur_bridge->clientid = NULL;
						cur_bridge->port = 0;
						cur_bridge->topics = NULL;
						cur_bridge->topic_count = 0;
						cur_bridge->topic_remapping = false;
						cur_bridge->restart_t = 0;
						cur_bridge->username = NULL;
						cur_bridge->password = NULL;
						cur_bridge->notifications = true;
						cur_bridge->notification_topic = NULL;
						cur_bridge->start_type = bst_automatic;
						cur_bridge->idle_timeout = 60;
						cur_bridge->restart_timeout = 30;
						cur_bridge->threshold = 10;
						cur_bridge->try_private = true;
#ifdef WITH_TLS
						cur_bridge->tls_cafile = NULL;
						cur_bridge->tls_capath = NULL;
						cur_bridge->tls_certfile = NULL;
						cur_bridge->tls_keyfile = NULL;
#endif
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty connection value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "connection_messages")){
					if(_conf_parse_bool(&token, token, &config->connection_messages, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "crlfile")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_string(&token, "crlfile", &cur_listener->crlfile, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "idle_timeout")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_int(&token, "idle_timeout", &cur_bridge->idle_timeout, saveptr)) return MOSQ_ERR_INVAL;
					if(cur_bridge->idle_timeout < 1){
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "idle_timeout interval too low, using 1 second.");
						cur_bridge->idle_timeout = 1;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "include_dir")){
					if(level == 0){
						/* Only process include_dir from the main config file. */
						token = strtok_r(NULL, " ", &saveptr);
#ifdef WIN32
						snprintf(dirpath, MAX_PATH, "%s\\*.conf", token);
						fh = FindFirstFile(dirpath, &find_data);
						if(fh == INVALID_HANDLE_VALUE){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Unable to open include_dir '%s'.", token);
							return 1;
						}

						do{
							len = strlen(token)+1+strlen(find_data.cFileName)+1;
							conf_file = _mosquitto_calloc(len+1, sizeof(char));
							if(!conf_file){
								FindClose(fh);
								return MOSQ_ERR_NOMEM;
							}
							snprintf(conf_file, len, "%s\\%s", token, find_data.cFileName);
								
							rc = _config_read_file(config, reload, conf_file, cr, level+1);
							_mosquitto_free(conf_file);
							if(rc){
								FindClose(fh);
								return rc;
							}
						}while(FindNextFile(fh, &find_data));

						FindClose(fh);
#else
						dh = opendir(token);
						if(!dh){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Unable to open include_dir '%s'.", token);
							return 1;
						}
						while((de = readdir(dh)) != NULL){
							if(strlen(de->d_name) > 5){
								if(!strcmp(&de->d_name[strlen(de->d_name)-5], ".conf")){
									len = strlen(token)+1+strlen(de->d_name)+1;
									conf_file = _mosquitto_calloc(len+1, sizeof(char));
									if(!conf_file){
										closedir(dh);
										return MOSQ_ERR_NOMEM;
									}
									snprintf(conf_file, len, "%s/%s", token, de->d_name);
									
									rc = _config_read_file(config, reload, conf_file, cr, level+1);
									_mosquitto_free(conf_file);
									if(rc){
										closedir(dh);
										return rc;
									}
								}
							}
						}
						closedir(dh);
#endif
					}
				}else if(!strcmp(token, "keepalive_interval")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_int(&token, "keepalive_interval", &cur_bridge->keepalive, saveptr)) return MOSQ_ERR_INVAL;
					if(cur_bridge->keepalive < 5){
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "keepalive interval too low, using 5 seconds.");
						cur_bridge->keepalive = 5;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "keyfile")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_string(&token, "keyfile", &cur_listener->keyfile, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "listener")){
					if(reload) continue; // Listeners not valid for reloading.
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						config->listener_count++;
						config->listeners = _mosquitto_realloc(config->listeners, sizeof(struct _mqtt3_listener)*config->listener_count);
						if(!config->listeners){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
							return MOSQ_ERR_NOMEM;
						}
						port_tmp = atoi(token);
						if(port_tmp < 1 || port_tmp > 65535){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
							return MOSQ_ERR_INVAL;
						}
						cur_listener = &config->listeners[config->listener_count-1];
						cur_listener->mount_point = NULL;
						cur_listener->port = port_tmp;
						cur_listener->socks = NULL;
						cur_listener->sock_count = 0;
						cur_listener->client_count = 0;
#ifdef WITH_TLS
						cur_listener->cafile = NULL;
						cur_listener->capath = NULL;
						cur_listener->certfile = NULL;
						cur_listener->keyfile = NULL;
						cur_listener->ciphers = NULL;
						cur_listener->psk_hint = NULL;
						cur_listener->require_certificate = false;
						cur_listener->ssl_ctx = NULL;
						cur_listener->crlfile = NULL;
#endif
						token = strtok_r(NULL, " ", &saveptr);
						if(token){
							cur_listener->host = _mosquitto_strdup(token);
						}else{
							cur_listener->host = NULL;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty listener value in configuration.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "log_dest")){
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						cr->log_dest_set = 1;
						if(!strcmp(token, "none")){
							cr->log_dest = MQTT3_LOG_NONE;
						}else if(!strcmp(token, "syslog")){
							cr->log_dest |= MQTT3_LOG_SYSLOG;
						}else if(!strcmp(token, "stdout")){
							cr->log_dest |= MQTT3_LOG_STDOUT;
						}else if(!strcmp(token, "stderr")){
							cr->log_dest |= MQTT3_LOG_STDERR;
						}else if(!strcmp(token, "topic")){
							cr->log_dest |= MQTT3_LOG_TOPIC;
						}else{
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid log_dest value (%s).", token);
							return MOSQ_ERR_INVAL;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty log_dest value in configuration.");
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "log_timestamp")){
					if(_conf_parse_bool(&token, token, &config->log_timestamp, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "log_type")){
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						cr->log_type_set = 1;
						if(!strcmp(token, "none")){
							cr->log_type = MOSQ_LOG_NONE;
						}else if(!strcmp(token, "information")){
							cr->log_type |= MOSQ_LOG_INFO;
						}else if(!strcmp(token, "notice")){
							cr->log_type |= MOSQ_LOG_NOTICE;
						}else if(!strcmp(token, "warning")){
							cr->log_type |= MOSQ_LOG_WARNING;
						}else if(!strcmp(token, "error")){
							cr->log_type |= MOSQ_LOG_ERR;
						}else if(!strcmp(token, "debug")){
							cr->log_type |= MOSQ_LOG_DEBUG;
						}else{
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid log_type value (%s).", token);
							return MOSQ_ERR_INVAL;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty log_type value in configuration.");
					}
				}else if(!strcmp(token, "max_connections")){
					if(reload) continue; // Listeners not valid for reloading.
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						cur_listener->max_connections = atoi(token);
						if(cur_listener->max_connections < 0) cur_listener->max_connections = -1;
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty max_connections value in configuration.");
					}
				}else if(!strcmp(token, "max_inflight_messages")){
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						cr->max_inflight_messages = atoi(token);
						if(cr->max_inflight_messages < 0) cr->max_inflight_messages = 0;
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty max_inflight_messages value in configuration.");
					}
				}else if(!strcmp(token, "max_queued_messages")){
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						cr->max_queued_messages = atoi(token);
						if(cr->max_queued_messages < 0) cr->max_queued_messages = 0;
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty max_queued_messages value in configuration.");
					}
				}else if(!strcmp(token, "mount_point")){
					if(reload) continue; // Listeners not valid for reloading.
					if(config->listener_count == 0){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: You must use create a listener before using the mount_point option in the configuration file.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_string(&token, "mount_point", &cur_listener->mount_point, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "notifications")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_bool(&token, "notifications", &cur_bridge->notifications, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "notification_topic")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_string(&token, "notification_topic", &cur_bridge->notification_topic, saveptr)) return MOSQ_ERR_INVAL;
					if(_mosquitto_fix_sub_topic(&cur_bridge->notification_topic)){
						return 1;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "password")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->password){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate password value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->password = _mosquitto_strdup(token);
						if(!cur_bridge->password){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty password value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "password_file")){
					if(reload){
						if(config->password_file){
							_mosquitto_free(config->password_file);
							config->password_file = NULL;
						}
					}
					if(_conf_parse_string(&token, "password_file", &config->password_file, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "persistence") || !strcmp(token, "retained_persistence")){
					if(_conf_parse_bool(&token, token, &config->persistence, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "persistence_file")){
					if(_conf_parse_string(&token, "persistence_file", &config->persistence_file, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "persistence_location")){
					if(_conf_parse_string(&token, "persistence_location", &config->persistence_location, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "persistent_client_expiration")){
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						switch(token[strlen(token)-1]){
							case 'd':
								expiration_mult = 86400;
								break;
							case 'w':
								expiration_mult = 86400*7;
								break;
							case 'm':
								expiration_mult = 86400*30;
								break;
							case 'y':
								expiration_mult = 86400*365;
								break;
							default:
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid persistent_client_expiration duration in configuration.");
								return MOSQ_ERR_INVAL;
						}
						token[strlen(token)-1] = '\0';
						config->persistent_client_expiration = atoi(token)*expiration_mult;
						if(config->persistent_client_expiration <= 0){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid persistent_client_expiration duration in configuration.");
							return MOSQ_ERR_INVAL;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty persistent_client_expiration value in configuration.");
					}
				}else if(!strcmp(token, "pid_file")){
					if(reload) continue; // pid file not valid for reloading.
					if(_conf_parse_string(&token, "pid_file", &config->pid_file, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "port")){
					if(reload) continue; // Listener not valid for reloading.
					if(config->default_listener.port){
						_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Default listener port specified multiple times. Only the latest will be used.");
					}
					if(_conf_parse_int(&token, "port", &port_tmp, saveptr)) return MOSQ_ERR_INVAL;
					if(port_tmp < 1 || port_tmp > 65535){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid port value (%d).", port_tmp);
						return MOSQ_ERR_INVAL;
					}
					config->default_listener.port = port_tmp;
				}else if(!strcmp(token, "psk_file")){
#if defined(WITH_TLS) && defined(WITH_TLS_PSK)
					if(reload) continue; // Listeners not valid for reloading.
					if(cur_listener->cafile || config->default_listener.capath){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Cannot use both certificate and psk encryption in a single listener.");
						return MOSQ_ERR_INVAL;
					}
					if(reload){
						if(config->psk_file){
							_mosquitto_free(config->psk_file);
							config->psk_file = NULL;
						}
					}
					if(_conf_parse_string(&token, "psk_file", &config->psk_file, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS/TLS-PSK support not available.");
#endif
				}else if(!strcmp(token, "psk_hint")){
#if defined(WITH_TLS) && defined(WITH_TLS_PSK)
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_string(&token, "psk_hint", &cur_listener->psk_hint, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS/TLS-PSK support not available.");
#endif
				}else if(!strcmp(token, "queue_qos0_messages")){
					if(_conf_parse_bool(&token, token, &config->queue_qos0_messages, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "require_certificate")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_bool(&token, "require_certificate", &cur_listener->require_certificate, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "restart_timeout")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_int(&token, "restart_timeout", &cur_bridge->restart_timeout, saveptr)) return MOSQ_ERR_INVAL;
					if(cur_bridge->restart_timeout < 1){
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "restart_timeout interval too low, using 1 second.");
						cur_bridge->restart_timeout = 1;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "retry_interval")){
					if(_conf_parse_int(&token, "retry_interval", &config->retry_interval, saveptr)) return MOSQ_ERR_INVAL;
					if(config->retry_interval < 1 || config->retry_interval > 3600){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid retry_interval value (%d).", config->retry_interval);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "start_type")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(!strcmp(token, "automatic")){
							cur_bridge->start_type = bst_automatic;
						}else if(!strcmp(token, "lazy")){
							cur_bridge->start_type = bst_lazy;
						}else if(!strcmp(token, "manual")){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Manual start_type not supported.");
							return MOSQ_ERR_INVAL;
						}else if(!strcmp(token, "once")){
							cur_bridge->start_type = bst_once;
						}else{
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid start_type value in configuration (%s).", token);
							return MOSQ_ERR_INVAL;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty start_type value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "store_clean_interval")){
					if(_conf_parse_int(&token, "store_clean_interval", &config->store_clean_interval, saveptr)) return MOSQ_ERR_INVAL;
					if(config->store_clean_interval < 0 || config->store_clean_interval > 65535){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid store_clean_interval value (%d).", config->store_clean_interval);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "sys_interval")){
					if(_conf_parse_int(&token, "sys_interval", &config->sys_interval, saveptr)) return MOSQ_ERR_INVAL;
					if(config->sys_interval < 1 || config->sys_interval > 65535){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid sys_interval value (%d).", config->sys_interval);
						return MOSQ_ERR_INVAL;
					}
				}else if(!strcmp(token, "threshold")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_int(&token, "threshold", &cur_bridge->threshold, saveptr)) return MOSQ_ERR_INVAL;
					if(cur_bridge->threshold < 1){
						_mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "threshold too low, using 1 message.");
						cur_bridge->threshold = 1;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "topic")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						cur_bridge->topic_count++;
						cur_bridge->topics = _mosquitto_realloc(cur_bridge->topics, 
								sizeof(struct _mqtt3_bridge_topic)*cur_bridge->topic_count);
						if(!cur_bridge->topics){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
						cur_topic = &cur_bridge->topics[cur_bridge->topic_count-1];
						if(!strcmp(token, "\"\"")){
							cur_topic->topic = NULL;
						}else{
							cur_topic->topic = _mosquitto_strdup(token);
							if(!cur_topic->topic){
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
								return MOSQ_ERR_NOMEM;
							}
							if(_mosquitto_fix_sub_topic(&cur_topic->topic)){
								return 1;
							}
						}
						cur_topic->direction = bd_out;
						cur_topic->qos = 0;
						cur_topic->local_prefix = NULL;
						cur_topic->remote_prefix = NULL;
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty topic value in configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(!strcasecmp(token, "out")){
							cur_topic->direction = bd_out;
						}else if(!strcasecmp(token, "in")){
							cur_topic->direction = bd_in;
						}else if(!strcasecmp(token, "both")){
							cur_topic->direction = bd_both;
						}else{
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge topic direction '%s'.", token);
							return MOSQ_ERR_INVAL;
						}
						token = strtok_r(NULL, " ", &saveptr);
						if(token){
							cur_topic->qos = atoi(token);
							if(cur_topic->qos < 0 || cur_topic->qos > 2){
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge QoS level '%s'.", token);
								return MOSQ_ERR_INVAL;
							}

							token = strtok_r(NULL, " ", &saveptr);
							if(token){
								cur_bridge->topic_remapping = true;
								if(!strcmp(token, "\"\"")){
									cur_topic->local_prefix = NULL;
								}else{
									if(_mosquitto_topic_wildcard_len_check(token) != MOSQ_ERR_SUCCESS){
										_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge topic local prefix '%s'.", token);
										return MOSQ_ERR_INVAL;
									}
									cur_topic->local_prefix = _mosquitto_strdup(token);
									if(!cur_topic->local_prefix){
										_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
										return MOSQ_ERR_NOMEM;
									}
								}

								token = strtok_r(NULL, " ", &saveptr);
								if(token){
									if(!strcmp(token, "\"\"")){
										cur_topic->remote_prefix = NULL;
									}else{
										if(_mosquitto_topic_wildcard_len_check(token) != MOSQ_ERR_SUCCESS){
											_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge topic remote prefix '%s'.", token);
											return MOSQ_ERR_INVAL;
										}
										cur_topic->remote_prefix = _mosquitto_strdup(token);
										if(!cur_topic->remote_prefix){
											_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
											return MOSQ_ERR_NOMEM;
										}
									}
								}
							}
						}
					}
					if(cur_topic->topic == NULL && 
							(cur_topic->local_prefix == NULL || cur_topic->remote_prefix == NULL)){

						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge remapping.");
						return MOSQ_ERR_INVAL;
					}
					if(cur_topic->local_prefix){
						if(cur_topic->topic){
							len = strlen(cur_topic->topic) + strlen(cur_topic->local_prefix)+1;
							cur_topic->local_topic = _mosquitto_calloc(len+1, sizeof(char));
							if(!cur_topic->local_topic){
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
								return MOSQ_ERR_NOMEM;
							}
							snprintf(cur_topic->local_topic, len+1, "%s%s", cur_topic->local_prefix, cur_topic->topic);
						}else{
							cur_topic->local_topic = _mosquitto_strdup(cur_topic->local_prefix);
							if(!cur_topic->local_topic){
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
								return MOSQ_ERR_NOMEM;
							}
						}
					}else{
						cur_topic->local_topic = _mosquitto_strdup(cur_topic->topic);
						if(!cur_topic->local_topic){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}
					if(cur_topic->local_topic){
						if(_mosquitto_fix_sub_topic(&cur_topic->local_topic)){
							return 1;
						}
					}

					if(cur_topic->remote_prefix){
						if(cur_topic->topic){
							len = strlen(cur_topic->topic) + strlen(cur_topic->remote_prefix)+1;
							cur_topic->remote_topic = _mosquitto_calloc(len+1, sizeof(char));
							if(!cur_topic->remote_topic){
								return MOSQ_ERR_NOMEM;
							}
							snprintf(cur_topic->remote_topic, len, "%s%s", cur_topic->remote_prefix, cur_topic->topic);
						}else{
							cur_topic->remote_topic = _mosquitto_strdup(cur_topic->remote_prefix);
							if(!cur_topic->remote_topic){
								_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
								return MOSQ_ERR_NOMEM;
							}
						}
					}else{
						cur_topic->remote_topic = _mosquitto_strdup(cur_topic->topic);
						if(!cur_topic->remote_topic){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}
					if(cur_topic->remote_topic){
						if(_mosquitto_fix_sub_topic(&cur_topic->remote_topic)){
							return 1;
						}
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "try_private")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					if(_conf_parse_bool(&token, "try_private", &cur_bridge->try_private, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "use_identity_as_username")){
#ifdef WITH_TLS
					if(reload) continue; // Listeners not valid for reloading.
					if(_conf_parse_bool(&token, "use_identity_as_username", &cur_listener->use_identity_as_username, saveptr)) return MOSQ_ERR_INVAL;
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: TLS support not available.");
#endif
				}else if(!strcmp(token, "user")){
					if(reload) continue; // Drop privileges user not valid for reloading.
					if(_conf_parse_string(&token, "user", &config->user, saveptr)) return MOSQ_ERR_INVAL;
				}else if(!strcmp(token, "username")){
#ifdef WITH_BRIDGE
					if(reload) continue; // FIXME
					if(!cur_bridge){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid bridge configuration.");
						return MOSQ_ERR_INVAL;
					}
					token = strtok_r(NULL, " ", &saveptr);
					if(token){
						if(cur_bridge->username){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate username value in bridge configuration.");
							return MOSQ_ERR_INVAL;
						}
						cur_bridge->username = _mosquitto_strdup(token);
						if(!cur_bridge->username){
							_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
							return MOSQ_ERR_NOMEM;
						}
					}else{
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty username value in configuration.");
						return MOSQ_ERR_INVAL;
					}
#else
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Bridge support not available.");
#endif
				}else if(!strcmp(token, "trace_level")
						|| !strcmp(token, "addresses")
						|| !strcmp(token, "round_robin")
						|| !strcmp(token, "ffdc_output")
						|| !strcmp(token, "max_log_entries")
						|| !strcmp(token, "trace_output")){
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Unsupported rsmb configuration option \"%s\".", token);
				}else{
					_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Unknown configuration variable \"%s\".", token);
					return MOSQ_ERR_INVAL;
				}
			}
		}
	}
	fclose(fptr);

	return MOSQ_ERR_SUCCESS;
}

static int _conf_parse_bool(char **token, const char *name, bool *value, char *saveptr)
{
	*token = strtok_r(NULL, " ", &saveptr);
	if(*token){
		if(!strcmp(*token, "false") || !strcmp(*token, "0")){
			*value = false;
		}else if(!strcmp(*token, "true") || !strcmp(*token, "1")){
			*value = true;
		}else{
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid %s value (%s).", name, *token);
		}
	}else{
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty %s value in configuration.", name);
		return MOSQ_ERR_INVAL;
	}
	
	return MOSQ_ERR_SUCCESS;
}

static int _conf_parse_int(char **token, const char *name, int *value, char *saveptr)
{
	*token = strtok_r(NULL, " ", &saveptr);
	if(*token){
		*value = atoi(*token);
	}else{
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty %s value in configuration.", name);
		return MOSQ_ERR_INVAL;
	}

	return MOSQ_ERR_SUCCESS;
}

static int _conf_parse_string(char **token, const char *name, char **value, char *saveptr)
{
	*token = strtok_r(NULL, " ", &saveptr);
	if(*token){
		if(*value){
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Duplicate %s value in configuration.", name);
			return MOSQ_ERR_INVAL;
		}
		*value = _mosquitto_strdup(*token);
		if(!*value){
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory");
			return MOSQ_ERR_NOMEM;
		}
	}else{
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Empty %s value in configuration.", name);
		return MOSQ_ERR_INVAL;
	}
	return MOSQ_ERR_SUCCESS;
}
