/**
 * Copyright (C), 2014-2015.
 * @file  
 * This file implements the apis to handle requests.
 */

#include <assert.h>
#include <stdlib.h>
#include "afs_adaptor.h"
#include "afs_request.h"
#include "afs_server.h"
#include "s5utils.h"
#include "spy.h"
#include "s5message.h"
#include "afs_flash_store.h"
#include "afs_cluster.h"

extern struct afsc_st afsc;

static int32_t read_op_count;
static int32_t write_op_count;
static int32_t reply_ok_count;
static int32_t delete_op_count;
static int32_t client_info_op_count;

struct flash_store flash_stores[16]; //support at most 16 store
static int store_count = 0;


static int32_t calculate_tray_id(uint64_t volume_id)
{
	return (int32_t)(volume_id & 0xff);
}

void register_spy_variables()
{
	spy_register_variable("read", &read_op_count, vt_int32, "read op count");
	spy_register_variable("write", &write_op_count, vt_int32, "write op count");
	spy_register_variable("reply_ok", &reply_ok_count, vt_int32, "OK replied to client");
	spy_register_variable("delete", &delete_op_count, vt_int32, "delete op count");
	spy_register_variable("client_info", &client_info_op_count, vt_int32, "client info op count");

}

void unregister_spy_variables()
{
	spy_unregister("read");
	spy_unregister("write");
	spy_unregister("reply_ok");
	spy_unregister("delete");
	spy_unregister("client_info");

}
int flash_store_config(struct toedaemon* toe_daemon, conf_file_t fp)
{
	store_count = toe_daemon->tray_set_count;
	char tary_section[16];
	for (int i = 0; i < store_count; i++)
	{
		snprintf(tary_section, sizeof(tary_section), "tray.%d", i);
		const char* dev_name = conf_get(fp, tary_section, "dev");
		if (dev_name == NULL)
		{
			S5LOG_ERROR("Failed to find key(dev_name) for section(%s) in S5 daemon conf(%s).",
				tary_section, toe_daemon->s5daemon_conf_file);
			return -EINVAL;
		}
		int rc = fs_init(toe_daemon->mngt_ip, &flash_stores[i], dev_name);
		if (rc)
		{
			S5LOG_ERROR("Failed to initialize %s, with dev_name(%s)", tary_section, dev_name);
		}
		register_tray(toe_daemon->mngt_ip, flash_stores[i].uuid, dev_name, flash_stores[i].dev_capacity);
		if(rc == 0)
			set_tray_state(toe_daemon->mngt_ip, flash_stores[i].uuid, TS_OK, TRUE);
		else
			set_tray_state(toe_daemon->mngt_ip, flash_stores[i].uuid, TS_ERROR, FALSE);
	}
	return 0;
}

/**
 * Send read reply message, as the unit of 4K. 
 * The message type could be: MSG_TYPE_READ or MSG_TYPE_S5_STAT
 */
void reply_message_read_ok_in_4k(s5_message_t* msg, const char* data, uint32_t len, PS5CLTSOCKET socket)
{
	s5_atomic_add(&reply_ok_count, 1);
	s5_message_t *msg_reply = s5msg_create(0);
	S5ASSERT(msg->head.msg_type == MSG_TYPE_READ ||
		  msg->head.msg_type == MSG_TYPE_S5_STAT ||
	 	  msg->head.msg_type == MSG_TYPE_NIC_CLIENT_INFO);
	memcpy(&msg_reply->head, &msg->head, sizeof(s5_message_head_t));
	msg_reply->head.status = MSG_STATUS_OK;
	msg_reply->head.msg_type = msg->head.msg_type + 1;
	for (int i = 0; i != msg->head.nlba; ++i)
	{
		msg_reply->head.slba = msg->head.slba + i;
		msg_reply->head.nlba = 1;

		if( i * LBA_LENGTH < len)
		{
			msg_reply->head.data_len = LBA_LENGTH;
			msg_reply->data = (void*)(data + i * LBA_LENGTH);
		}
		else
		{
			msg_reply->head.data_len = 0;
			msg_reply->data = NULL;

		}
		s5socket_send_msg(socket, msg_reply);
	}
	free(msg_reply);
	msg_reply = NULL;
}

void reply_message_status(s5_message_t* msg, uint16_t status, PS5CLTSOCKET socket)
{
	s5_atomic_add(&reply_ok_count,1);

	s5_message_t *msg_reply = s5msg_create(0);
	memcpy(&msg_reply->head, &msg->head, sizeof(s5_message_head_t));
	msg_reply->head.msg_type = msg->head.msg_type + 1; //reply 
	msg_reply->head.status = status;
	msg_reply->head.data_len = 0;
	int rc = s5socket_send_msg(socket, msg_reply);
	if(rc < 0)
	{
		S5LOG_ERROR("Failed to send message reply tid[%d] rc(%d).", msg_reply->head.transaction_id, rc);
	}
	s5msg_release_all(&msg_reply);
}




int cachemgr_write_request(s5_message_t *msg, PS5CLTSOCKET socket)
{

	int rc = 0;
	int tray_index = calculate_tray_id(msg->head.volume_id);
	s5_atomic_add(&write_op_count,1);
	S5ASSERT(msg->head.nlba == 1);
	rc = fs_write(&flash_stores[tray_index], msg->head.volume_id, msg->head.slba, msg->head.snap_seq, msg->head.nlba, msg->data);
	
	if(rc >= 0)
	{
		reply_message_status(msg, MSG_STATUS_OK, socket);
		//S5LOG_DEBUG("Reply write ok[in cache]! transaction_id:%d", msg->head.transaction_id);
		return rc;
	}

	switch(rc)
	{
	case -ENOMEM:
		reply_message_status(msg, MSG_STATUS_NOSPACE, socket);
		break;

	default:
		S5LOG_FATAL("Failed: unexpected cache_write return value rc(%d).", rc);
		S5ASSERT(0);
	}
	return rc;
}

int cachemgr_block_delete_request(s5_message_t *msg, PS5CLTSOCKET socket)
{
	int rc = 0;
	s5_atomic_add(&delete_op_count,1);

	int tray_index = calculate_tray_id(msg->head.volume_id);
	S5ASSERT(msg->head.nlba == 1);
	rc = fs_delete_node(&flash_stores[tray_index], msg->head.volume_id, msg->head.slba, msg->head.snap_seq, msg->head.nlba);
	reply_message_status(msg, MSG_STATUS_OK, socket);
	return rc;
}



int cachemgr_read_request(s5_message_t *msg, PS5CLTSOCKET socket)
{
	int rc = 0;
	int tray_index = calculate_tray_id(msg->head.volume_id);
	char buf[4*1024];
	s5_atomic_add(&read_op_count,1);
	S5ASSERT(msg->head.nlba == 1);
	rc = fs_read(&flash_stores[tray_index], msg->head.volume_id, msg->head.slba, msg->head.snap_seq, msg->head.nlba, buf);
	
	if(rc >= 0)
	{
		reply_message_read_ok_in_4k(msg, buf, (uint32_t)rc, socket);
		//S5LOG_DEBUG("Reply read ok! transaction_id:%d", msg->head.transaction_id);
		return rc;
	}

	switch(rc){
		case -ENOENT: //not exist and node ready
			reply_message_read_ok_in_4k(msg, buf, 0, socket);
			//S5LOG_DEBUG("Read data not exist [ENOENT], transaction_id:%d", msg->head.transaction_id);
			break;
		case -ENOMEM:
			reply_message_status(msg, MSG_STATUS_NOSPACE, socket);
			S5LOG_ERROR("Read (tid:%d) ENOMEM, return MSG_STATUS_NOSPACE", msg->head.transaction_id);
			break;
		default:
			S5ASSERT(0);
	}
	return rc;
}

