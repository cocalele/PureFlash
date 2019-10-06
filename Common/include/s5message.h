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

#define MAX_NAME_LEN 96	///< max length of name used in s5 modules.
#define	S5MESSAGE_MAGIC		0x3553424e	///< magic number for s5 message.

#define S5_DAEMON_PORT    3000  ///< default s5daemon listen port
#define TOE_SERVER_FOR_BD_PORT 10000 ///< default toe listen port.
#define	LBA_LENGTH		4096	///< LBA's length.
#define LBA_LENGTH_ORDER 12
#define	S5_OBJ_LEN		(4*1024*1024)	///< S5 object's length.


/**
 *   Message type of S5 protocol.
 */
typedef enum  msg_type
{
	MSG_TYPE_READ 				=0,	///< Read request.
	MSG_TYPE_READ_REPLY 		=1,	///< Read reply.
	MSG_TYPE_WRITE				=2,	///< Write request.
	MSG_TYPE_WRITE_REPLY		=3,	///< Read reply.
	MSG_TYPE_LOADWRITE			=4,	///< Load write request, backend send to S5.
	MSG_TYPE_LOADWRITE_REPLY	=5,	///< Load write reply, S5 send to backend.
	MSG_TYPE_FLUSHCOMPLETE		=6,	///< Flush complete request, backend send to S5.
	MSG_TYPE_FLUSHCOMPLETE_REPLY=7,	///< Flush complete reply, S5 send to backend.
	MSG_TYPE_CACHEDELETE		=8,	///< Cache delete request, send to S5 to delete special cache item.
	MSG_TYPE_CACHEDELETE_REPLY	=9,	///< Cache delete reply, come from S5.
	MSG_TYPE_KEEPALIVE			=10,	///< Keep alive request, send to S5 to probe the connection is valid yes or no.
	MSG_TYPE_KEEPALIVE_REPLY	=11,	///< Keep alive reply.
	MSG_TYPE_CACHEFIND			=12,	///< Cache find request, send to S5 to probe the cache exist or no.
	MSG_TYPE_CACHEFIND_REPLY	=13,	///< Cache find reply.
	MSG_TYPE_FLUSH_READ 		=14,	///< Flush read request, backend send to S5 to flush cache.
	MSG_TYPE_FLUSH_READ_REPLY 	=15,	///< Flush read reply, S5 send to backend.

	MSG_TYPE_LOADWRITE_COMPLETE			=24,	///< Load write complete request, backend send to S5.
	MSG_TYPE_LOADWRITE_COMPLETE_REPLY	=25,	///< Load write complete reply, S5 send to backend.

	MSG_TYPE_OPENIMAGE					=32,	///< Open volume request.
	MSG_TYPE_OPENIMAGE_REPLY			=33,	///< Open volume reply.
	MSG_TYPE_CLOSEIMAGE					=34,	///< Close volume request.
	MSG_TYPE_CLOSEIMAGE_REPLY			=35,	///< Close volume reply.
	MSG_TYPE_TRIM						=36,	///< Trim request.
	MSG_TYPE_TRIM_REPLY					=37,	///< Trim reply.
	MSG_TYPE_FLUSH_REQUEST				=38,	///< Flush request.
	MSG_TYPE_FLUSH_REPLY				=39, 	///< Flush reply, verify received request, not to be used currently.
	MSG_TYPE_LOAD_REQUEST				=40,	///< Load request.
	MSG_TYPE_LOAD_REPLY					=41,	///< Load reply, verify received request, not to be  used currently.
	MSG_TYPE_SNAP_CHANGED				=42,	///< Snapshot changed notification.
	MSG_TYPE_SNAP_CHANGED_REPLY			=43,	///< Snapshot changed reply, verify received, not to be  used currently.

	MSG_TYPE_S5CLT_REQ 					=50,
	MSG_TYPE_S5CLT_REPLY 				=51,
	MSG_TYPE_RGE_BLOCK_DELETE			=52,	///< Delete RGE/S5Afs cache block request.
	MSG_TYPE_RGE_BLOCK_DELETE_REPLY		=53,	///< Delete RGE/S5Afs cache block reply.
	MSG_TYPE_S5_STAT					=54,	///< Stat S5 request.
	MSG_TYPE_S5_STAT_REPLY				=55,	///< Stat S5 reply.
    MSG_TYPE_NIC_CLIENT_INFO            =56,    ///< Stat client info request.
    MSG_TYPE_NIC_CLIENT_INFO_REPLY      =57,    ///< Stat client info reply.

	MSG_TYPE_CMETA_REQ					=512,	///< msg request from conductor to daemon
	MSG_TYPE_CMETA_REPLY				=513,	///< msg reply from daemon to conductor
	MSG_TYPE_MQCLUSTER_CHANGE			=514,	///< msg constructed by broker to notify conductor, or constucted by worker to notify daemon
	MSG_TYPE_NOTIFY_MQCLUSTER			=515,	///< msg constructed by conductor or daemon to notify broker or worker
	MSG_TYPE_DSTATUS_CHANGE_NOTIFY		=516,	///< notify from daemon to conductor
	MSG_TYPE_DSTATUS_CHANGE_REPLY		=517,	///< reply of notify from daemon to conductor
	MSG_TYPE_MAX
} msg_type_t;

/**
 * Get the name of s5message's type, get the string name refer to enum type.
 *
 * @param[in] 	msg_tpype	status enum type.
 * @return 	status's name on success, UNKNOWN_TYPE on failure.
 * @retval	MSG_TYPE_READ							MSG_TYPE_READ.
 * @retval	MSG_TYPE_READ_REPLY					MSG_TYPE_READ_REPLY.
 * @retval	MSG_TYPE_WRITE						MSG_TYPE_WRITE.
 * @retval	MSG_TYPE_WRITE_REPLY					MSG_TYPE_WRITE_REPLY.
 * @retval	MSG_TYPE_LOADWRITE					MSG_TYPE_LOADWRITE.
 * @retval	MSG_TYPE_LOADWRITE_REPLY				MSG_TYPE_LOADWRITE_REPLY.
 * @retval	MSG_TYPE_FLUSHCOMPLETE				MSG_TYPE_FLUSHCOMPLETE.
 * @retval	MSG_TYPE_FLUSHCOMPLETE_REPLY			MSG_TYPE_FLUSHCOMPLETE_REPLY.
 * @retval	MSG_TYPE_CACHEDELETE					MSG_TYPE_CACHEDELETE.
 * @retval	MSG_TYPE_CACHEDELETE_REPLY			MSG_TYPE_CACHEDELETE_REPLY.
 * @retval	MSG_TYPE_KEEPALIVE					MSG_TYPE_KEEPALIVE.
 * @retval	MSG_TYPE_KEEPALIVE_REPLY				MSG_TYPE_KEEPALIVE_REPLY.
 * @retval	MSG_TYPE_CACHEFIND					MSG_TYPE_CACHEFIND.
 * @retval	MSG_TYPE_CACHEFIND_REPLY				MSG_TYPE_CACHEFIND_REPLY.
 * @retval	MSG_TYPE_FLUSH_READ					MSG_TYPE_FLUSH_READ.
 * @retval	MSG_TYPE_FLUSH_READ_REPLY			MSG_TYPE_FLUSH_READ_REPLY.
 * @retval	MSG_TYPE_LOADWRITE_COMPLETE			MSG_TYPE_LOADWRITE_COMPLETE.
 * @retval	MSG_TYPE_LOADWRITE_COMPLETE_REPLY	MSG_TYPE_LOADWRITE_COMPLETE_REPLY.
 * @retval	MSG_TYPE_OPENIMAGE					MSG_TYPE_OPENIMAGE.
 * @retval	MSG_TYPE_OPENIMAGE_REPLY				MSG_TYPE_OPENIMAGE_REPLY.
 * @retval	MSG_TYPE_CLOSEIMAGE					MSG_TYPE_CLOSEIMAGE.
 * @retval	MSG_TYPE_CLOSEIMAGE_REPLY			MSG_TYPE_CLOSEIMAGE_REPLY.
 * @retval	MSG_TYPE_TRIM							MSG_TYPE_TRIM.
 * @retval	MSG_TYPE_TRIM_REPLY					MSG_TYPE_TRIM_REPLY.
 * @retval	MSG_TYPE_FLUSH_REQUEST				MSG_TYPE_FLUSH_REQUEST.
 * @retval	MSG_TYPE_FLUSH_REPLY					MSG_TYPE_FLUSH_REPLY.
 * @retval	MSG_TYPE_LOAD_REQUEST				MSG_TYPE_LOAD_REQUEST.
 * @retval	MSG_TYPE_LOAD_REPLY					MSG_TYPE_LOAD_REPLY.
 * @retval	MSG_TYPE_SNAP_CHANGED				MSG_TYPE_SNAP_CHANGED.
 * @retval	MSG_TYPE_SNAP_CHANGED_REPLY			MSG_TYPE_SNAP_CHANGED_REPLY.
 * @retval	MSG_TYPE_S5CLT_REPLY					MSG_TYPE_S5CLT_REPLY.
 * @retval	MSG_TYPE_RGE_BLOCK_DELETE			MSG_TYPE_RGE_BLOCK_DELETE.
 * @retval	MSG_TYPE_RGE_BLOCK_DELETE_REPLY		MSG_TYPE_RGE_BLOCK_DELETE_REPLY.
 * @retval	MSG_TYPE_S5_STAT						MSG_TYPE_S5_STAT.
 * @retval	MSG_TYPE_S5_STAT_REPLY				MSG_TYPE_S5_STAT_REPLY.
 * @retval	MSG_TYPE_MAX							MSG_TYPE_MAX.
 * @retval	UNKNOWN_STATUS						msg_tpype is invalid.
 */
const char* get_msg_type_name(msg_type_t msg_tpype);

typedef enum msg_status
{
	MSG_STATUS_ERR				=-1,	///< Generally error.
	MSG_STATUS_OK 				=0,		///< OK.
	MSG_STATUS_DELAY_RETRY		=3,		///< delay retry, can not to be handle on time, need to retry.
	MSG_STATUS_REPLY_FLUSH		=5,		///< flush reply, only used in ic logic.
	MSG_STATUS_REPLY_LOAD		=6,		///< load reply, only used in ic logic.
	MSG_STATUS_NOSPACE			=8,		///< no space can be used.
	MSG_STATUS_RETRY_LOAD		=9,		///< load retry, can not to be handle on time, need to retry.

	/**
	 *   reserve to 127 for ic logic.
	 */
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
	MSG_STATUS_MAX
} msg_status_t;

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
const char* get_msg_status_name(msg_status_t msg_status);


#pragma pack(1)

/**
 *   s5message's head data structure definition.
 */
struct s5_message_head
{
	uint32_t		magic_num;		///< 0x3553424e, magic number.
	uint32_t		msg_type;		///< type.
	uint32_t		transaction_id;	///< transaction id. bit[0~8]:ictx->node_cache index, bit[9~21]:submitted io counter, bit[22~31]:submitted io index
	uint64_t		slba;			///< start of LBA.
	uint32_t		data_len;		///< the length of  s5message's data, unit is byte.
	uint64_t		volume_id;		///< volume id, s5message assosiated.
	uint32_t		user_id;		///< user id, not to be used currently.
	uint32_t		pool_id;		///< pool id, not to be used currently.
	uint32_t		nlba;			///< count of LBAs.
	uint32_t		obj_ver;		///< s5 block object version, not to be used currently.
	uint32_t		listen_port;	///< listen port for receiving snap_change_notify in open-image listen_port for toe port in open-image-reply.
	uint32_t		snap_seq;		///< snap sequence, not to be used currently.
	uint32_t		status;			///< handle status of this s5message, valid in reply message..
	uint8_t         iops_density;	///< iops density.
	uint8_t         is_head;		///< 0: is not head version, 1: is head version.
	uint8_t         read_unit;		///< 0:read block size(4KB) 1:read block size(8KB) 2:read block size(16KB).
	uint8_t		    reserve[1];		///< reserve.
};

static_assert(sizeof(s5_message_head) == 64);

/**
 * s5message's data structure definition.
 */
struct s5_message
{
    s5_message_head head; ///< s5message head field.
    void* data;             ///< s5message data field.
};

struct s5_handshake_message {
	int16_t recfmt;
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
static_assert(sizeof(struct s5_handshake_message) == 32);
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


