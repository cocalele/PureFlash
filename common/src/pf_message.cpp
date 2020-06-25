

#include "pf_message.h"


const char* PfOpCode2Str(PfOpCode op)
{
	switch (op)
	{
	case S5_OP_READ:
		return "MSG_TYPE_READ";
	case S5_OP_WRITE:
		return "MSG_TYPE_WRITE";
	case S5_OP_REPLICATE_WRITE:
		return "S5_OP_REPLICATE_WRITE";
	case S5_OP_COW_READ:
		return "S5_OP_COW_READ";
	case S5_OP_COW_WRITE:
		return "S5_OP_COW_WRITE";
	case S5_OP_RECOVERY_READ:
		return "S5_OP_RECOVERY_READ";
	case S5_OP_RECOVERY_WRITE:
		return "S5_OP_RECOVERY_WRITE";
	case S5_OP_HEARTBEAT:
		return "S5_OP_HEARTBEAT";

	default:
		return "UNKNOWN_TYPE";
		break;
	}
}


const char* get_msg_status_name(PfMessageStatus msg_st)
{
	static __thread char buf[64];
	sprintf(buf, "%d", msg_st);
	return buf;
}

