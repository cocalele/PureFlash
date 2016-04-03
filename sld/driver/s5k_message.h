#ifndef __S5K_MESSAGE_H__
#define __S5K_MESSAGE_H__

#include <linux/types.h>
#include <linux/bio.h>

#include "s5k_basetype.h"

#ifndef MAX_NAME_LEN
#define MAX_NAME_LEN 96
#endif

#ifndef MAX_IP_LENGTH
#define MAX_IP_LENGTH 16
#endif

#define	S5MESSAGE_MAGIC	0x3553424e

#define LBA_LENGTH_ORDER 12							///< the power number of 2 about LBA Length.
#define	LBA_LENGTH		4096
#define	S5_OBJ_LEN		(4*1024*1024)

#define	MESSAGE_TIMEOUT 5
#define RESEND_ON_TIMEOUT


/**
 *   Message type of S5 protocol.
 */
typedef enum  msg_type
{
	MSG_TYPE_READ 				= 0,
	MSG_TYPE_READ_REPLY 		= 1,
	MSG_TYPE_WRITE				= 2,
	MSG_TYPE_WRITE_REPLY		= 3,
	MSG_TYPE_LOADWRITE_nouse			= 4,
	MSG_TYPE_LOADWRITE_REPLY	= 5,
	MSG_TYPE_FLUSHCOMPLETE		= 6,
	MSG_TYPE_FLUSHCOMPLETE_REPLY = 7,
	MSG_TYPE_CACHEDELETE		= 8,
	MSG_TYPE_CACHEDELETE_REPLY	= 9,
	MSG_TYPE_KEEPALIVE			= 10,
	MSG_TYPE_KEEPALIVE_REPLY	= 11,
	MSG_TYPE_CACHEFIND			= 12,
	MSG_TYPE_CACHEFIND_REPLY	= 13,
	MSG_TYPE_FLUSH_READ 		= 14,
	MSG_TYPE_FLUSH_READ_REPLY 	= 15,

	MSG_TYPE_LOADWRITE_COMPLETE			= 24,
	MSG_TYPE_LOADWRITE_COMPLETE_REPLY	= 25,


	MSG_TYPE_OPENIMAGE			= 32,
	MSG_TYPE_OPENIMAGE_REPLY	= 33,
	MSG_TYPE_CLOSEIMAGE			= 34,
	MSG_TYPE_CLOSEIMAGE_REPLY	= 35,
	MSG_TYPE_TRIM				= 36,
	MSG_TYPE_TRIM_REPLY			= 37,
	MSG_TYPE_FLUSH_REQUEST		= 38,
	MSG_TYPE_FLUSH_REPLY		= 39, //not used
	MSG_TYPE_LOAD_REQUEST		= 40,
	MSG_TYPE_LOAD_REPLY			= 41, //not used
	MSG_TYPE_SNAP_CHANGED		= 42,
	MSG_TYPE_SNAP_CHANGED_REPLY	= 43,	//adding for send ack
	MSG_TYPE_GET_SYSINFO		= 44,
	MSG_TYPE_GET_SYSINFO_REPLY	= 45,
	MSG_TYPE_GET_STASTICINFO	= 46,
	MSG_TYPE_GET_STASTICINFO_REPLY	= 47,
	MSG_TYPE_GET_IMAGE_META		= 48,
	MSG_TYPE_GET_IMAGE_META_REPLY	= 49,
	MSG_TYPE_S5META_ACCESS 			= 50,
	MSG_TYPE_S5META_ACCESS_REPLY 	= 51,
	MSG_TYPE_S5VOLUME_REQ				= 52,
	MSG_TYPE_S5VOLUME_REPLY 			= 53,
	MSG_TYPE_MAX
} msg_type_t;

//const char* get_msg_type_name(msg_type_t msg_tp);

typedef enum msg_status
{
	MSG_STATUS_ERR				= -1,
	MSG_STATUS_OK 				= 0,
	MSG_STATUS_DELAY_RETRY		= 3,
	MSG_STATUS_REPLY_FLUSH		= 5,
	MSG_STATUS_REPLY_LOAD		= 6,
	MSG_STATUS_NOSPACE			= 8,
	MSG_STATUS_RETRY_LOAD		= 9,
	//reserve to 127 for ic logic.

	MSG_STATUS_AUTH_ERR 		= 128,
	MSG_STATUS_VER_MISMATCH		= 129,
	//
	MSG_STATUS_CANCEL_FLUSH		= 131,
	MSG_STATUS_CRC_ERR			= 132,
	MSG_STATUS_OPENIMAGE_ERR	= 133,
	MSG_STATUS_NOTFOUND			= 134, ///cache entry not found, for cache find
	MSG_STATUS_RCV_ERR			= 135,
	MSG_STATUS_SND_ERR			= 136,
	MSG_STATUS_CONNECT_ERR		= 137,
	MSG_STATUS_BIND_ERR			= 138,
	MSG_STATUS_NET_ERR			= 139,
	MSG_STATUS_CONF_ERR			= 140, //s5.conf err.
	MSG_STATUS_MAX
} msg_status_t;

//const char* get_msg_status_name(msg_status_t msg_st);

#define is_write(msg_type) ((msg_type) == MSG_TYPE_WRITE) //load_write not used now
/*algined by 1 byte.*/
/*======================================================*/
typedef struct s5_message_head
{
	int32_t		magic_num;	/*0x3553424e*/
	int32_t		msg_type;
	volatile int32_t		transaction_id;
	uint64_t		slba;		/*start of LBA*/
	int32_t		data_len;
	uint64_t		image_id;
	int32_t		user_id;
	union
	{
		int32_t		pool_id;
		int32_t		timestamp;
	};

	int32_t		nlba;		/*count of LBAs*/
	int32_t		obj_ver;		/*utilized on flush or load*/
	int32_t		listen_port;	/*listen_port for receiving snap_chage_notify in Login
								  *listen_port for toe port in Login_reply*/
	int32_t		snap_seq;
	int32_t		status;
	uchar       iops_density;
	uchar       is_head;		/*0: is not head version, 1: is head version*/
	uchar       read_unit;		/*0:read block size(4KB) 1:read block size(8KB)	2:read block size(16KB)*/
	char		reserve[1];
} __attribute__((packed))s5_message_head_t;

typedef struct s5_message_tail
{
	int32_t		flag;/*bit0: discard message yes or no
					    *bit1: with crc yes or no
					    */
	int32_t		crc;
} __attribute__((packed))s5_message_tail_t;


#define LOCK_TID_BIT 	0
#define LOCK_FIFO_BIT 	0

typedef struct s5k_tid_info
{
	volatile struct bio	* bio;
	volatile uint64_t slba;
    volatile struct bio_vec *bvec;
	volatile uint64_t start_jiffies;
	volatile uint32_t tid;


	uint8_t bv_cnt;
	uint8_t read_unit;
	uint32_t timeout_count;
	uint32_t retry_count;
	int32_t finish_id;

	/* must put tid_lock_atomic in the last of this struct */
	volatile unsigned long tid_lock_atomic;
	volatile unsigned long fifo_lock_atomic;
	/* use bit 0 to a atomic operation
	 * to protect tid conflict in recv thread and time out check
	 * test_and_set_bit()
	 */
} __attribute__((packed))s5k_tid_info_t;

typedef struct s5_message
{
    s5_message_head_t head; ///< s5message head field.
    s5_message_tail_t tail; ///< s5message tail field.
    void* data;             ///< s5message data field.
} __attribute__((packed))s5_message_t;

#endif	//__S5K_MESSAGE_H__
