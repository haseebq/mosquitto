#include <stdlib.h>

#include <mqtt3.h>

mqtt3_context *mqtt3_init_context(int sock)
{
	mqtt3_context *context;

	context = malloc(sizeof(mqtt3_context));
	if(!context) return NULL;
	
	context->next = NULL;
	context->sock = sock;
	context->last_msg_in = time(NULL);
	context->last_msg_out = time(NULL);
	context->keepalive = 60; /* Default to 60s */
	context->last_mid = 0;
	context->id = NULL;
	context->messages = NULL;

	return context;
}

void mqtt3_cleanup_context(mqtt3_context *context)
{
	if(!context) return;

	if(context->sock != -1){
		mqtt3_close_socket(context);
	}
	if(context->id) free(context->id);
	/* FIXME - clean messages and subscriptions */
	free(context);
}

