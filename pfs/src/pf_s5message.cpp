#include <stdlib.h>
#include <string.h>
#include "pf_message.h"

int s5msg_release_all(pf_message_t ** msg)
{
	//release msg
	if (*msg)
	{
		if ((*msg)->data)
		{
			free((*msg)->data);
			(*msg)->data = NULL;
		}
		free((*msg));
		(*msg) = NULL;
	}
	return 0;
}

/**
 * @return a s5 message object allocated on heap,
 *         NULL on error. errno set to ENOMEM on error
 */
pf_message_t* s5msg_create(int data_size)
{
	if(data_size < 0)
		return NULL;
	pf_message_t* msg = (pf_message_t*)malloc(sizeof(pf_message_t));
	if (!msg)
	{
		return NULL;
	}
	memset(msg, 0, sizeof(pf_message_t));

	msg->head.magic_num = S5MESSAGE_MAGIC;
	msg->head.data_len = data_size;
	if (data_size > 0)
	{
		msg->data = malloc((size_t)data_size);
		if (!msg->data)
		{
			free(msg);
			msg = NULL;
			return msg;
		}
		memset(msg->data, 0, (size_t)data_size);
	}
	else
	{
		msg->data = NULL;
	}

	return msg;
}

const char* get_msg_type_name(msg_type_t msg_tp)
{
	switch (msg_tp)
	{
	case MSG_TYPE_READ:
		return "MSG_TYPE_READ";
	case MSG_TYPE_READ_REPLY:
		return "MSG_TYPE_READ_REPLY";
	case MSG_TYPE_WRITE:
		return "MSG_TYPE_WRITE";
	case MSG_TYPE_WRITE_REPLY:
		return "MSG_TYPE_WRITE_REPLY";
	case MSG_TYPE_LOADWRITE:
		return "MSG_TYPE_LOADWRITE";
	case MSG_TYPE_LOADWRITE_REPLY:
		return "MSG_TYPE_LOADWRITE_REPLY";
	case MSG_TYPE_FLUSHCOMPLETE:
		return "MSG_TYPE_FLUSHCOMPLETE";
	case MSG_TYPE_FLUSHCOMPLETE_REPLY:
		return "MSG_TYPE_FLUSHCOMPLETE_REPLY";
	case MSG_TYPE_CACHEDELETE:
		return "MSG_TYPE_CACHEDELETE";
	case MSG_TYPE_CACHEDELETE_REPLY:
		return "MSG_TYPE_CACHEDELETE_REPLY";
	case MSG_TYPE_KEEPALIVE:
		return "MSG_TYPE_KEEPALIVE";
	case MSG_TYPE_KEEPALIVE_REPLY:
		return "MSG_TYPE_KEEPALIVE_REPLY";
	case MSG_TYPE_CACHEFIND:
		return "MSG_TYPE_CACHEFIND";
	case MSG_TYPE_CACHEFIND_REPLY:
		return "MSG_TYPE_CACHEFIND_REPLY";
	case MSG_TYPE_FLUSH_READ:
		return "MSG_TYPE_FLUSH_READ";
	case MSG_TYPE_FLUSH_READ_REPLY:
		return "MSG_TYPE_FLUSH_READ_REPLY";
	case MSG_TYPE_LOADWRITE_COMPLETE:
		return "MSG_TYPE_LOADWRITE_COMPLETE";
	case MSG_TYPE_LOADWRITE_COMPLETE_REPLY:
		return "MSG_TYPE_LOADWRITE_COMPLETE_REPLY";
	case MSG_TYPE_OPENIMAGE:
		return "MSG_TYPE_OPENIMAGE";
	case MSG_TYPE_OPENIMAGE_REPLY:
		return "MSG_TYPE_OPENIMAGE_REPLY";
	case MSG_TYPE_CLOSEIMAGE:
		return "MSG_TYPE_CLOSEIMAGE";
	case MSG_TYPE_CLOSEIMAGE_REPLY:
		return "MSG_TYPE_CLOSEIMAGE_REPLY";
	case MSG_TYPE_TRIM:
		return "MSG_TYPE_TRIM";
	case MSG_TYPE_TRIM_REPLY:
		return "MSG_TYPE_TRIM_REPLY";
	case MSG_TYPE_FLUSH_REQUEST:
		return "MSG_TYPE_FLUSH_REQUEST";
	case MSG_TYPE_FLUSH_REPLY:
		return "MSG_TYPE_FLUSH_REPLY";
	case MSG_TYPE_LOAD_REQUEST:
		return "MSG_TYPE_LOAD_REQUEST";
	case MSG_TYPE_LOAD_REPLY:
		return "MSG_TYPE_LOAD_REPLY";
	case MSG_TYPE_SNAP_CHANGED:
		return "MSG_TYPE_SNAP_CHANGED";
	case MSG_TYPE_SNAP_CHANGED_REPLY:
		return "MSG_TYPE_SNAP_CHANGED_REPLY";
	case MSG_TYPE_S5CLT_REQ:
		return "MSG_TYPE_S5CLT_REQ";

	case MSG_TYPE_S5CLT_REPLY:
		return "MSG_TYPE_S5CLT_REPLY";
	case MSG_TYPE_RGE_BLOCK_DELETE:
		return "MSG_TYPE_RGE_BLOCK_DELETE";
	case MSG_TYPE_RGE_BLOCK_DELETE_REPLY:
		return "MSG_TYPE_RGE_BLOCK_DELETE_REPLY";
	case MSG_TYPE_S5_STAT:
		return "MSG_TYPE_S5_STAT";
	case MSG_TYPE_S5_STAT_REPLY:
		return "MSG_TYPE_S5_STAT_REPLY";
	case MSG_TYPE_MAX:
		return "MSG_TYPE_MAX";

	default:
		return "UNKNOWN_TYPE";
		break;
	}
}


const char* get_msg_status_name(msg_status_t msg_st)
{
	switch (msg_st)
	{
	case MSG_STATUS_ERR:
		return "MSG_STATUS_ERR";
	case MSG_STATUS_OK:
		return "MSG_STATUS_OK";
	case MSG_STATUS_DELAY_RETRY:
		return "MSG_STATUS_DELAY_RETRY";
	case MSG_STATUS_REPLY_FLUSH:
		return "MSG_STATUS_REPLY_FLUSH";
	case MSG_STATUS_REPLY_LOAD:
		return "MSG_STATUS_REPLY_LOAD";
	case MSG_STATUS_NOSPACE:
		return "MSG_STATUS_NOSPACE";
	case MSG_STATUS_RETRY_LOAD:
		return "MSG_STATUS_RETRY_LOAD";

	case MSG_STATUS_AUTH_ERR:
		return "MSG_STATUS_AUTH_ERR";
	case MSG_STATUS_VER_MISMATCH:
		return "MSG_STATUS_VER_MISMATCH";
	case MSG_STATUS_CANCEL_FLUSH:
		return "MSG_STATUS_CANCEL_FLUSH";
	case MSG_STATUS_CRC_ERR:
		return "MSG_STATUS_CRC_ERR";
	case MSG_STATUS_OPENIMAGE_ERR:
		return "MSG_STATUS_OPENIMAGE_ERR";
	case MSG_STATUS_NOTFOUND:
		return "MSG_STATUS_NOTFOUND";
	case MSG_STATUS_BIND_ERR:
		return "MSG_STATUS_BIND_ERR";
	case MSG_STATUS_NET_ERR:
		return "MSG_STATUS_NET_ERR";
	case MSG_STATUS_CONF_ERR:
		return "MSG_STATUS_CONF_ERR";
	case MSG_STATUS_INVAL:
		return "MSG_STATUS_INVAL";

	case MSG_STATUS_MAX:
		return "MSG_STATUS_MAX";
	default:
		return "UNKNOWN_STATUS";
	}
}

