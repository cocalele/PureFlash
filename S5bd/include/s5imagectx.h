/**
 * Copyright (C), 2014-2015.
 * @file
 * This file declares the s5 volume context, and APIs to operate s5 volume context.
 */

#ifndef __LIBS5BD_S5IMAGECTX_H__
#define __LIBS5BD_S5IMAGECTX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include "tasknode.h"
#include "s5socket.h"
#include "s5log.h"
#include "s5session.h"
#include "idgenerator.h"
#include "s5aiocompletion.h"
#include "s5conf.h"
#include "s5_meta.h"
#include "s5_context.h"

/**
 * brief Macro defines the mutex number
 *
 * Defines the total mutex number for block node. If working block node number is larger than it,
 * other block node should wait.
 */
#define NODE_MTX_NUM 256

#define SUFFIX_STAT_VAR_LEN     128

struct s5_aiocompletion;
typedef struct s5_volume_ctx
{
//volume staff
	pthread_spinlock_t io_num_lock;// lock to protect io_num processing
	size_t io_num;
	struct s5_unitnode* node_cache;// array of unit node, number = s5_io_depth+1
	idgenerator idg;
	struct s5_blocknode** slotlist;//volume_size/4M
	uint64 node_slot_num;//slotlist size(volume_size/4M)
	uint16 node_mtx_num;//min(NODE_MTX_NUM, volume_size/4M)
	pthread_spinlock_t* node_mtx; //lock to protect BlockNode.
	s5session_conf_t session_conf;
	int replica_num;
	int	replica_ctx_id[MAX_REPLICA_NUM]; //a magic number need send back to S5, client don't care it
	int nic_ip_blacklist_len[MAX_REPLICA_NUM];
	ipv4addr_buf_t  nic_ip_blacklist[MAX_REPLICA_NUM][MAX_NIC_IP_BLACKLIST_LEN];

//conductor related
	PS5CLTSOCKET conductor_clt;
	pthread_t stat_thread;
	ipv4addr_buf_t nic_ip[MAX_REPLICA_NUM];
	int32 nic_port[MAX_REPLICA_NUM];

	int32 user_id;
	uint64 replica_id[MAX_REPLICA_NUM]; //replicas id
	int32 snap_seq;//latest snap sequence
	uint64 volume_size;
	uint64 volume_id; 
	char volume_name[MAX_NAME_LEN];
	char tenant_name[MAX_NAME_LEN];
	char snap_name[MAX_NAME_LEN];
	BOOL read_only;
	volatile BOOL callback_exit_flag;
//rge related
	volatile int callback_deq_count;			///< the count dequeue from callback queue.
	volatile int callback_deq_count_interval;		///< the count dequeue interval, used to compute iops, latency.
	pthread_t callback_thread_id;				///< callback thread's id
	int callback_thread_eventfd;				///< callback thread event file descriptor. Used to wake up callback thread.
	int *s5io_cb_array; 					///< tid'counter array
	int read_session_idx;					///< the current index of session to read data
	s5_session_t session[MAX_REPLICA_NUM];

//stat related
	volatile unsigned band_width_interval;
	volatile unsigned band_width_per_second;
	volatile float iops_interval;
	uchar iops_per_GB;
	volatile long latency_interval;
	volatile float latency_per_second;
	char _suffix_stat_var[SUFFIX_STAT_VAR_LEN];

	s5_context_t* s5_context;
} s5_volume_ctx_t;

int32 s5_volumectx_init(struct s5_volume_ctx* ictx);

int32 s5_volumectx_release(struct s5_volume_ctx* ictx);

int32 s5_volumectx_slotlist_init(struct s5_volume_ctx* ictx);

void s5_volumectx_slotlist_release(struct s5_volume_ctx* ictx);

int32 s5_volumectx_node_mtx_init(struct s5_volume_ctx* ictx);

void s5_volumectx_node_mtx_release(struct s5_volume_ctx* ictx);

int32 s5_volumectx_node_cache_init(struct s5_volume_ctx* ictx);

void s5_volumectx_node_cache_release(struct s5_volume_ctx* ictx);

int32 s5_volumectx_open_volume_to_conductor(struct s5_volume_ctx* ictx, int index);

int32 s5_volumectx_close_volume_to_conductor(struct s5_volume_ctx* ictx);

int32 open_volume(struct s5_volume_ctx* ictx);

int32 close_volume(struct s5_volume_ctx* ictx);

int32 init_conf_server(struct s5_volume_ctx* ictx);

/**
 * Create the thread for statistic running information.
 *
 * @param[in] ictx   The pointer of volume context. It should be managed by the user.
 * @return int, it will return the result of pthread_create.
 */

int32 init_stat_thread(struct s5_volume_ctx* ictx);

int32 parse_open_volume_reply(struct s5_volume_ctx* ictx, s5_message_t *msg_reply, int replica_index);

int32 recv_msg_update_ctx(void* sockParam, s5_message_t* msg, void* param);

void build_msg(struct s5_unitnode* unode);

void send_unit_node(struct s5_unitnode* unode);

int32 dispatch_task(struct s5_blocknode *bnode);

void process_task(struct s5_unitnode *unode);

/**
 * The implementation of async read operation.
 *
 * @param[in]   ictx	            Volume context. It will be created when open a volume, and deleted when close a volume.
 * @param[in]   off         		The offset for the read operation in the volume. Unit: LBA(4k).
 * @param[in]   len					The read length in the unit of LBA(4k).
 * @param[out]   buf				The data buffer to get the read data. This buffer should be managed by the user.
 * @param[in]	 comp				User created aio completion
 */
void _aio_read(struct s5_volume_ctx *ictx, uint64_t off, size_t len, char *buf, struct s5_aiocompletion *comp);

/**
 * The implementation of async write operation.
 *
 * @param[in]   ictx                Volume context. It will be created when open a volume, and deleted when close a volume.
 * @param[in]   off                 The offset for the write operation in the volume. Unit: LBA(4k).
 * @param[in]   len                 The write length in the unit of LBA(4k).
 * @param[in]   buf                The data buffer for input write data. This buffer should be managed by the user.
 * @param[in]    comp               User created aio completion
 */
void _aio_write(struct s5_volume_ctx *ictx, uint64_t off, size_t len, const char *buf, struct s5_aiocompletion *comp);

/**
 * The implementation of async read operation.
 *
 * @param[in]   ictx                Volume context. It will be created when open a volume, and deleted when close a volume.
 * @param[in]   off                 The offset for the read operation in the volume. Unit: LBA(4k).
 * @param[in]   len                 The read length in the unit of LBA(4k).
 * @param[out]   buf                The data buffer to get the read data. This buffer should be managed by the user.
 * @return  read length when successful
 * @retval  read_length when read successful
 * @retval  -EINVAL If "off" is not LBA length divisible;
 *					If "off" equal or larger than volume size;
 *					If "len" is not LBA length divisible
 * @retval  -EAGAIN if current IO number is larger than S5 IO depth
 */
ssize_t _sio_read(struct s5_volume_ctx *ictx, uint64_t off, size_t len, char *buf);

/**
 * The implementation of async write operation.
 *
 * @param[in]   ictx                Volume context. It will be created when open a volume, and deleted when close a volume.
 * @param[in]   off                 The offset for the write operation in the volume. Unit: LBA(4k).
 * @param[in]   len                 The write length in the unit of LBA(4k).
 * @param[in]   buf                The data buffer for input write data. This buffer should be managed by the user.
 * @return  0 when successful
 * @retval  0 when write successful
 * @retval  -EINVAL If "off" is not LBA length divisible;
 * 					If "off" equal or larger than volume size;
 * 					If "len" is not LBA length divisible
 * @retval  -EAGAIN if current IO number is larger than S5 IO depth
 */
ssize_t _sio_write(struct s5_volume_ctx *ictx, uint64_t off, size_t len, const char *buf);


/**
 * check the input value and  update th io_num 
 *
 * @param[in]   ictx                Volume context. It will be created when open a volume, and deleted when close a volume.
 * @param[in]   off                 The offset for the write operation in the volume. Unit: LBA(4k).
 * @param[in,out]   len                 The write length in the unit of LBA(4k).
 * @return  0 update successful
 * @retval  0 update successful
 * @retval  -EINVAL If "off" is not LBA length divisible, or equal or larger than volume size.
 * 					If "len" is not LBA length divisible
 * @retval	-EAGAIN if current IO number is larger than S5 IO depth
 */

int update_io_num_and_len(struct s5_volume_ctx *ictx, uint64_t off, size_t *len);


#ifdef __cplusplus
}
#endif

#endif
