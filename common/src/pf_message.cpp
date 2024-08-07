

#include "pf_message.h"


const char* PfOpCode2Str(PfOpCode op)
{
	switch (op)
	{
	case S5_OP_READ:
		return "S5_OP_READ";
	case S5_OP_WRITE:
		return "S5_OP_WRITE";
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

#define C_NAME(x) case x: return #x;
const char* PfMessageStatus2Str(PfMessageStatus msg_st)
{
	static __thread char buf[64];
	switch(msg_st) {
		C_NAME(MSG_STATUS_SUCCESS)
		C_NAME(MSG_STATUS_INVALID_OPCODE)
		C_NAME(MSG_STATUS_INVALID_FIELD)
		C_NAME(MSG_STATUS_CMDID_CONFLICT)
		C_NAME(MSG_STATUS_DATA_XFER_ERROR)
		C_NAME(MSG_STATUS_POWER_LOSS)
		C_NAME(MSG_STATUS_INTERNAL)
		C_NAME(MSG_STATUS_ABORT_REQ)
		C_NAME(MSG_STATUS_INVALID_IO_TIMEOUT)
		C_NAME(MSG_STATUS_INVALID_STATE)
		C_NAME(MSG_STATUS_LBA_RANGE)
		C_NAME(MSG_STATUS_NS_NOT_READY)
		C_NAME(MSG_STATUS_NOT_PRIMARY)
		C_NAME(MSG_STATUS_NOSPACE)
		C_NAME(MSG_STATUS_READONLY)
		C_NAME(MSG_STATUS_CONN_LOST)
		C_NAME(MSG_STATUS_AIOERROR)
		C_NAME(MSG_STATUS_ERROR_HANDLED)
		C_NAME(MSG_STATUS_ERROR_UNRECOVERABLE)
		C_NAME(MSG_STATUS_AIO_TIMEOUT)
		C_NAME(MSG_STATUS_REPLICATING_TIMEOUT)
		C_NAME(MSG_STATUS_NODE_LOST)
		C_NAME(MSG_STATUS_LOGFAILED)
		C_NAME(MSG_STATUS_METRO_REPLICATING_FAILED)
		C_NAME(MSG_STATUS_RECOVERY_FAILED)
		C_NAME(MSG_STATUS_SSD_ERROR)
		C_NAME(MSG_STATUS_REP_TO_PRIMARY)
		C_NAME(MSG_STATUS_NO_RESOURCE)
		C_NAME(MSG_STATUS_DEGRADE)
		C_NAME(MSG_STATUS_REOPEN)
		default:
			sprintf(buf, "Unknown status:%d", msg_st);
			return buf;
	}
//	MSG_STATUS_DEGRADE = 0x2000,
//	MSG_STATUS_REOPEN = 0x4000,

}

