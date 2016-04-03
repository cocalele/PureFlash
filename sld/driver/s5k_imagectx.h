#ifndef __S5K_IMAGECTX_H__
#define __S5K_IMAGECTX_H__

#include <linux/kfifo.h>
#include <linux/blkdev.h>
#include <linux/in.h>
#include <linux/timer.h>
#include <linux/kref.h>
#include "s5k_message.h"
#include "s5ioctl.h"

#define TID_DEPTH   		64

#define MAX_4K_CNT			1  //accept 4 4k page in one bio

#define SUFFIX_STAT_VAR_LEN     128
#define S5BD_MAX_NAME_LEN	128
#define	S5BD_MAX_UUID_LEN	(S5BD_MAX_NAME_LEN+1)
#define	S5BD_MAX_VENDOR_LEN	S5BD_MAX_NAME_LEN

typedef char ipv4addr_buf_t[IPV4_ADDR_LEN];

struct s5bd_attr
{
	char name[S5BD_MAX_NAME_LEN];	/* device name */
	char uuid[S5BD_MAX_UUID_LEN];	/* unique identifier for
				 * the block device */
	char vendor[S5BD_MAX_VENDOR_LEN];
};

typedef struct s5_imagectx
{
	//s5 related
	struct device_info dinfo;

	int volume_ctx_id; /* conductor use this id */
	int nic_ip_blacklist_len;
	ipv4addr_buf_t nic_ip_blacklist[MAX_NIC_IP_BLACKLIST_LEN];

//	uint32_t user_id;
	uint64_t volume_id;
	int32_t snap_seq;//latest snap sequence
	uint64_t volume_size;

	//block device related.
	struct block_device_operations fops;
	struct request_queue *queue;
	struct gendisk *disk;
	struct kref kref;

	//rge related.
	uint32_t toe_ip;
	char local_ip[MAX_IP_LENGTH];
	uint32_t daemon_port;
	uint32_t toe_port;
	struct msghdr s5k_tcp_rx_msghdr;
	struct msghdr s5k_tcp_tx_msghdr;

	struct task_struct *send_kthread_id;
	wait_queue_head_t send_kthread_wq;

	struct task_struct *recv_kthread_id;
	struct completion recv_kthread_completion;
	struct socket *socket_fd;
	struct sockaddr_in toe_server_addr;

//	atomic_t retrying_count;

	BOOL reconnect;
	BOOL kill_all_bio;

	//statistic related.
#if 0
	uint64_t sendr;
	uint64_t sendw;
	uint64_t recvr;
	uint64_t recvw;
	uint64_t timeoutr;
	uint64_t timeoutw;
	uint64_t ionum;
	uint64_t connect_times;
	uint64_t discardmsg;
	uint64_t retry_num;
#endif

//	unsigned band_width_interval;
//	unsigned band_width_per_second;
	/**io per N seconds*/
//	unsigned iopns;
	/**4k count, use to calc band width*/
//	unsigned cnt_4k;
//	unsigned iops_interval;
//	uchar iops_per_GB;
//	unsigned int latency_interval;
//	unsigned int latency_per_second;

	//char _suffix_stat_var[SUFFIX_STAT_VAR_LEN];

	struct kobject s5bd_kobj;
	struct s5bd_attr	attr;

//	uint64_t last_call_socket_rcv_time;
//	uint64_t last_call_s5rcv_time;

   	struct bio_list send_bio_list;
   	struct bio_list timeout_retry_io_list;
	struct kfifo timeout_retry_fifo;
	spinlock_t lock_bio;
	spinlock_t lock_retry_timeout;

	s5k_tid_info_t tidinfo[TID_DEPTH];
	struct kfifo id_generator;
	spinlock_t lock_tid;

	uint32_t bio_finish_cnt[TID_DEPTH];
	struct kfifo finish_id_generator;

//	uint32_t tid_alloc_count;
//	uint32_t tid_reply_count;

	volatile uint16_t s5k_stamp;

	struct timer_list check_mon;

} s5_imagectx_t;


int32_t s5bd_open(struct s5_imagectx* ictx);
int32_t s5bd_close(struct s5_imagectx* ictx);

#endif //__S5K_IMAGECTX_H__
