#include <stdint.h>

#include <mqtt3.h>

const char *mqtt3_command_to_string(uint8_t command)
{
	switch(command){
		case CONNACK:
			return "CONNACK";
		case CONNECT:
			return "CONNECT";
		case DISCONNECT:
			return "DISCONNECT";
		case PINGREQ:
			return "PINGREQ";
		case PINGRESP:
			return "PINGRESP";
		case PUBACK:
			return "PUBACK";
		case PUBCOMP:
			return "PUBCOMP";
		case PUBLISH:
			return "PUBLISH";
		case PUBREC:
			return "PUBREC";
		case PUBREL:
			return "PUBREL";
		case SUBACK:
			return "SUBACK";
		case SUBSCRIBE:
			return "SUBSCRIBE";
		case UNSUBACK:
			return "UNSUBACK";
		case UNSUBSCRIBE:
			return "UNSUBSCRIBE";
	}
	return "UNKNOWN";
}

uint16_t mqtt3_generate_message_id(void)
{
	static uint16_t mid = 123;

	return ++mid;
}

