

#include "pf_message.h"


const char* get_msg_type_name(int msg_tp)
{
	switch (msg_tp)
	{
	case S5_OP_READ:
		return "MSG_TYPE_READ";
	case S5_OP_WRITE:
		return "MSG_TYPE_WRITE";


	default:
		return "UNKNOWN_TYPE";
		break;
	}
}


const char* get_msg_status_name(message_status msg_st)
{
	static __thread char buf[64];
	sprintf(buf, "%d", msg_st);
	return buf;
}

