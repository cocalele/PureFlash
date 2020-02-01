#ifndef __S5MESSAGE__
#define __S5MESSAGE__

/**
* Copyright (C), 2014-2015.
* @file
* s5 message definition.
*
* This file includes all s5 message data structures and interfaces, which are used by S5 modules.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/types.h>
#include <stdarg.h>
#include "basetype.h"

enum class Suit :int { Diamonds, Hearts, Clubs, Spades };

#define MAX_NAME_LEN 96	///< max length of name used in s5 modules.
#define	S5MESSAGE_MAGIC		0x3553424e	///< magic number for s5 message.

#define S5_OP_WRITE  0X01
#define S5_OP_READ  0X02
#define S5_OP_REPLICATE_WRITE  0X81
#define S5_OP_COW_READ  0X82
#define S5_OP_COW_WRITE  0X83
#define S5_OP_RECOVERY_READ  0X84
#define S5_OP_RECOVERY_WRITE  0X85
#define S5_OP_HEARTBEAT  0X86


enum msg_status {
	MSG_STATUS_OK 				=0,		///< OK.
	MSG_STATUS_ERR              =1,	    ///< Generally error.
	MSG_STATUS_DELAY_RETRY		=3,		///< delay retry, can not to be handle on time, need to retry.
	MSG_STATUS_REPLY_FLUSH		=5,		///< flush reply, only used in ic logic.
	MSG_STATUS_REPLY_LOAD		=6,		///< load reply, only used in ic logic.
	MSG_STATUS_NOSPACE			=8,		///< no space can be used.
	MSG_STATUS_RETRY_LOAD		=9,		///< load retry, can not to be handle on time, need to retry.

	MSG_STATUS_AUTH_ERR 		=128,	///< authority error.
	MSG_STATUS_VER_MISMATCH		=129,	///< version not match.
	MSG_STATUS_CANCEL_FLUSH		=131,	///< cancel the flush read request.
	MSG_STATUS_CRC_ERR			=132,	///< CRC error.
	MSG_STATUS_OPENIMAGE_ERR	=133,	///< error in open image.
	MSG_STATUS_NOTFOUND			=134, 	///< can not find.
	MSG_STATUS_BIND_ERR			=135,	///< socket can not bind.
	MSG_STATUS_NET_ERR			=139,	///< error in network.
	MSG_STATUS_CONF_ERR			=140,	///< error in configure file of s5.
	MSG_STATUS_INVAL			=141,	///< invalid parameters.
	MSG_STATUS_INVALID_IO_TIMEOUT = 142,
	MSG_STATUS_MAX
};

/**
 * Get the name of s5message's status, get the string name refer to enum type.
 *
 * @param[in] 	msg_status	status enum type.
 * @return 	status's name on success, UNKNOWN_STATUS on failure.
 * @retval	MSG_STATUS_ERR				MSG_STATUS_ERR.
 * @retval	MSG_STATUS_OK					MSG_STATUS_OK.
 * @retval	MSG_STATUS_DELAY_RETRY		MSG_STATUS_DELAY_RETRY.
 * @retval	MSG_STATUS_REPLY_FLUSH		MSG_STATUS_REPLY_FLUSH.
 * @retval	MSG_STATUS_REPLY_LOAD		MSG_STATUS_REPLY_LOAD.
 * @retval	MSG_STATUS_NOSPACE			MSG_STATUS_NOSPACE.
 * @retval	MSG_STATUS_RETRY_LOAD		MSG_STATUS_RETRY_LOAD.
 * @retval	MSG_STATUS_AUTH_ERR			MSG_STATUS_AUTH_ERR.
 * @retval	MSG_STATUS_VER_MISMATCH		MSG_STATUS_VER_MISMATCH.
 * @retval	MSG_STATUS_CANCEL_FLUSH		MSG_STATUS_CANCEL_FLUSH.
 * @retval	MSG_STATUS_CRC_ERR			MSG_STATUS_CRC_ERR.
 * @retval	MSG_STATUS_OPENIMAGE_ERR	MSG_STATUS_OPENIMAGE_ERR.
 * @retval	MSG_STATUS_NOTFOUND			MSG_STATUS_NOTFOUND.
 * @retval	MSG_STATUS_BIND_ERR			MSG_STATUS_BIND_ERR.
 * @retval	MSG_STATUS_NET_ERR			MSG_STATUS_NET_ERR.
 * @retval	MSG_STATUS_CONF_ERR			MSG_STATUS_CONF_ERR.
 * @retval	MSG_STATUS_INVAL				MSG_STATUS_INVAL.
 * @retval	UNKNOWN_STATUS				msg_status is invalid.
 */


#pragma pack(1)

/**
 *   s5message's head data structure definition.
 */
struct s5_message_head
{
	uint8_t                   opcode;
	uint8_t                   rsv1;
	uint16_t                  command_id;
	uint32_t                  snap_seq;
	uint64_t                  vol_id;
	uint64_t                  buf_addr;
	uint32_t                  rkey;
	uint32_t                  buf_len;
	uint64_t                  slba;
	uint16_t                  nlba;
	uint16_t                  meta_ver;
	uint32_t                  task_seq;  //use this field to hold io task sequence number
	uint64_t                  rsv2;
	uint64_t                  rsv3;
};

static_assert(sizeof(s5_message_head) == 64, "s5_message_head");


struct s5_message_reply {
	uint16_t  status;         /* did the command fail, and if so, why? */
	uint16_t  meta_ver;
	uint16_t  command_id;     /* of the command which completed */
	uint16_t  command_seq;
	uint64_t  rsv1;
	uint64_t  rsv2;
	uint64_t  rsv3;
};
static_assert(sizeof(s5_message_reply) == 32, "s5_message_reply");

struct s5_handshake_message {
	int16_t io_timeout;
	union {
		int16_t qid;
		int16_t crqsize; //server return this on accept's private data, indicates real IO depth
	};
	int16_t hrqsize;//host receive queue size
	int16_t hsqsize;//host send queue size, i.e. max IO queue depth for ULP
	uint64_t vol_id; //srv1 defined by NVMe over Fabric
	int32_t snap_seq;
	union {
		int16_t protocol_ver; //on client to server, this is protocol version
		int16_t hs_result; //on server to client, this is  shake result, 0 for success, others for failure.
	};
	uint16_t rsv1;
	uint64_t rsv2;
};
static_assert(sizeof(struct s5_handshake_message) == 32, "s5_handshake_message");
#pragma pack()


#define debug_data_len 10	///< debug data length.

/**
 * Dump data refer to debug_data_len.
 *
 * @param[in] data	 target data to dump.
 * @return 	pointer to string of debug_data_len's data.
 */
#define get_debug_data(data) ({                 \
	char buf[debug_data_len+1];               \
	do{                                       \
	memset(buf,0,debug_data_len+1);         \
	memcpy(buf,data,debug_data_len);        \
	}while(0);                                \
	buf;                                      \
})


#endif	/*__S5MESSAGE__*/


