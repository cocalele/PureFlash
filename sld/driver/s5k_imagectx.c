#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/kfifo.h>
#include <uapi/linux/time.h>

#include <net/tcp.h>
#include "s5k_imagectx.h"
#include "s5k_log.h"
#include "s5k_conductor.h"

/*
1. check timeout 的时候 recv_kthread 正好来了超时, 这个需要做好同步保护
2. 超时都发送出去了， 超时的又回来了。 需要检测保护
3. 一直不回了，包括retry的包，按照超时时间计算（5s）触发重传，重传了3次也未完成的，
	需要触发重新连接。重连再失败，就直接报BIO错误。重连成功，需要记录重连次数，重连成功3次，
	还没有发出去的io报EIO。
4.  socket返回错误的也报重连接。
5. BIO 报错只能在重连失败和读写有失败的时候才报， 别的情况下不能-EIO
6. submit bio 获取id不到的时候， 不能报错误， 有可能在重传
*/

#define CHECK_TIMER_FREQ (5*HZ)
#define BIO_RE_SEND_TIMEOUT_HZ (15*HZ)

#define RE_SEND_COUNTS_LIMIT 	2
#define RE_CONNECT_COUNTS_LIMIT 2

static volatile uint8_t discard_buf[4096];

#define TID_SEQ(tid) ((uint32_t)(tid) & 0xffff0000)
#define TID(tid) (tid & 0x0000ffff)


static int32_t s5k_connect_toe (struct s5_imagectx* ictx);
static int32_t s5k_send_kthread (void *arg);
static int32_t s5k_recv_kthread(void *data);
static int32_t s5k_aio_write (struct s5_imagectx *ictx,  s5k_tid_info_t * pidinfo);
static int32_t s5k_aio_read (struct s5_imagectx *ictx,  s5k_tid_info_t * pidinfo);
static void s5k_check_io_timeout(unsigned long);
static void s5k_bio_err_all(struct s5_imagectx *ictx);
static void s5k_retry_io_resend(struct s5_imagectx *ictx, uint32_t tid);
static void s5k_timeout_io_resend(struct s5_imagectx *ictx, uint32_t tid);
static void s5k_start_check_timer(struct s5_imagectx* ictx);
static void s5k_stop_check_timer(struct s5_imagectx* ictx);
extern void s5bd_end_io_acct(struct s5_imagectx *s5bd_dev,
	volatile struct bio *bio, uint32_t id);
extern int kernel_setsockopt(struct socket *sock, int level, int optname,
	char *optval, unsigned int optlen);
static int32_t s5k_start_recv_thread(struct s5_imagectx* ictx);
static int32_t s5k_get_toe_info(struct s5_imagectx* ictx);
static int32_t s5k_put_toe_info(struct s5_imagectx* ictx);

static inline uint32_t s5k_get_tid_seq(struct s5_imagectx* ictx)
{
	ictx->s5k_stamp++;
	if(ictx->s5k_stamp==0)
		ictx->s5k_stamp++;
	return TID_SEQ((ictx->s5k_stamp)<<16);
}

static inline BOOL s5k_test_set_bit (uint32_t bit_pos, void *addr)
{
	uint8_t ret;
	ret = test_and_set_bit(bit_pos, (void *) addr);
	return ret&0x1;
}

static inline BOOL s5k_test_clear_bit(uint32_t bit_pos, void *addr)
{
	uint8_t ret;
	ret = test_and_clear_bit(bit_pos, (void *) addr);
	return ret&0x1;
}

static inline void s5k_finish_statistic(struct s5_imagectx *ictx, struct bio * bio, int status)
{
	if (bio_data_dir(bio))
	{
		if(status)
			atomic_inc(&ictx->dinfo.dstat.bio_write_finished_error);
		else
			atomic_inc(&ictx->dinfo.dstat.bio_write_finished_ok);
	}
	else
	{
		if(status)
			atomic_inc(&ictx->dinfo.dstat.bio_read_finished_error);
		else
			atomic_inc(&ictx->dinfo.dstat.bio_read_finished_ok);
	}
}

static int32_t s5k_open_image(struct s5_imagectx* ictx)
{
	int32_t ret = -1;
	int32_t i = -1;

	ictx->nic_ip_blacklist_len = 0;
	ictx->volume_ctx_id = -1;

	spin_lock_init(&ictx->lock_bio);
	spin_lock_init(&ictx->lock_tid);
	spin_lock_init(&ictx->lock_retry_timeout);
	bio_list_init(&ictx->send_bio_list);

	/* this fifo save s5k_tid_info_t pointer */

    ret = kfifo_alloc(&ictx->timeout_retry_fifo,
    	(TID_DEPTH * sizeof(uint32_t)), GFP_KERNEL);
	if(ret != 0)
	{
		LOG_ERROR("Failed to alloc timeout fifo, ret(%d).", ret);
		return ret;
	}

	ret = kfifo_alloc(&ictx->id_generator, (sizeof(int32_t) * TID_DEPTH), GFP_KERNEL);
	if(ret != 0)
	{
		LOG_ERROR("Failed to alloc id generartor fifo ret(%d).", ret);
		return ret;
	}
	for(i = 0; i < TID_DEPTH; ++i)
	{
		ret = kfifo_in_spinlocked(&ictx->id_generator,
				&i, sizeof(int32_t), &ictx->lock_tid);
		S5ASSERT(ret == sizeof(int32_t));
	}


	ret = kfifo_alloc(&ictx->finish_id_generator, (sizeof(int32_t) * TID_DEPTH), GFP_KERNEL);
	if(ret != 0)
	{
		LOG_ERROR("Failed to alloc finish id fifo, ret(%d).", ret);
		return ret;
	}
	for(i = 0; i < TID_DEPTH; ++i)
	{
		ret = kfifo_in(&ictx->finish_id_generator, &i, sizeof(int32_t));
		S5ASSERT(ret == sizeof(int32_t));
	}

	ret = s5k_connect_toe (ictx);
	if(ret != 0)
	{
		LOG_ERROR("Failed to connect server(0x%x:%d).", ictx->toe_ip,
			ictx->toe_port);
		kfifo_free(&ictx->timeout_retry_fifo);
		return ret;
	}

	ret = s5k_start_recv_thread(ictx);
	if (ret != 0)
	{
		kfifo_free(&ictx->timeout_retry_fifo);
		return ret;
	}

	LOG_INFO("Succeed to connect to server(0x%x:%d).", ictx->toe_ip,
		ictx->toe_port);

	init_waitqueue_head(&ictx->send_kthread_wq);
	ictx->send_kthread_id = kthread_run(s5k_send_kthread, ictx,
		"send_kthread");

	if(IS_ERR(ictx->send_kthread_id))
	{
		LOG_ERROR("Failed call kthread_run s5k_send_kthread.");
		ret = -EBUSY;
		kfifo_free(&ictx->timeout_retry_fifo);
		return ret;
	}

	s5k_start_check_timer(ictx);

	return 0;
}

int32_t s5k_close_image(struct s5_imagectx* ictx)
{
	int32_t ret = EOK;

	s5k_stop_check_timer(ictx);

	if (ictx->send_kthread_id)
	{
		kthread_stop(ictx->send_kthread_id);
		ictx->send_kthread_id = NULL;
	}

	//LOG_INFO("ictx->send_kthread_id %p ", ictx->send_kthread_id);

	if (ictx->socket_fd != NULL)
	{
		kernel_sock_shutdown(ictx->socket_fd, SHUT_RDWR);
		ictx->socket_fd = NULL;
	}

	wait_for_completion(&ictx->recv_kthread_completion);

	if (ictx->socket_fd != NULL)
	{
		sock_release(ictx->socket_fd);
		ictx->socket_fd = NULL;
	}

	kfifo_free(&ictx->timeout_retry_fifo);

	return ret;
}

int32_t s5bd_open(struct s5_imagectx* ictx)
{
	int32_t ret = 0;

	ret = s5k_open_image(ictx);
	if (ret != 0)
	{
		LOG_ERROR("Failed to Initialize device(%s), ret(%d).",
			ictx->dinfo.dev_name, ret);
		return ret;
	}

	LOG_INFO("Succeed to Initialize device(%s) size(%llu) io_depth(%d).",
		ictx->dinfo.dev_name, ictx->volume_size, TID_DEPTH);


	return ret;
}

int32_t s5bd_close(struct s5_imagectx* ictx)
{
	int32_t ret = 0;

	ret = s5k_close_image(ictx);
	if(ret)
	{
		LOG_ERROR("Failed to release device(%s), ret(%d).", ictx->dinfo.dev_name, ret);
	}
	else
	{
		LOG_INFO("Succeed to release device(%s).", ictx->dinfo.dev_name);
	}

	if(ret == 0)
	{
		ret = s5k_put_toe_info(ictx);
		if(ret)
		{
			LOG_ERROR("Failed to close volume to conductor, device(%s), ret(%d).",
					ictx->dinfo.dev_name, ret);
		}
		else
		{
			LOG_INFO("Succeed to close volume to conductor, device(%s).",
					ictx->dinfo.dev_name);
		}
	}

	return ret;
}

static void s5k_set_inetaddr(uint32_t sip, unsigned short port,
	struct sockaddr_in *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = sip;
	addr->sin_port = htons(port);
}

void s5k_start_check_timer(struct s5_imagectx* ictx)
{
	init_timer(&ictx->check_mon);
	ictx->check_mon.expires = jiffies + 5*HZ;
	ictx->check_mon.data = (unsigned long)ictx;
	ictx->check_mon.function = &s5k_check_io_timeout;
	add_timer(&ictx->check_mon);
}

void s5k_stop_check_timer(struct s5_imagectx* ictx)
{
	del_timer_sync(&ictx->check_mon);
}

static int32_t s5k_start_recv_thread(struct s5_imagectx* ictx)
{
	int32_t ret = 0;

	init_completion(&ictx->recv_kthread_completion);
	ictx->recv_kthread_id = kthread_run(s5k_recv_kthread, ictx, "recv_kthread");
	if (IS_ERR(ictx->recv_kthread_id))
	{
		LOG_ERROR("Failed call kthread_run recv_thread.");
		complete_all(&ictx->recv_kthread_completion);
		ret = -EBUSY;
		return ret;
	}
	return ret;
}

static int32_t s5k_get_toe_info(struct s5_imagectx* ictx)
{
	int32_t ret = 0;
	switch(ictx->dinfo.toe_use_mode)
	{
		case TOE_MODE_DEBUG:
			ictx->toe_ip = ictx->dinfo.mode_debug.toe_ip;
			ictx->toe_port = ictx->dinfo.mode_debug.toe_port;
			ictx->volume_id = ictx->dinfo.mode_debug.volume_id;
			ictx->volume_size = ictx->dinfo.mode_debug.volume_size;
			break;
		case TOE_MODE_CONDUCTOR:
			//get info from conductor.
			ret = cdkt_register_volume(ictx);
			break;
		default:
			S5ASSERT(0);
			break;
	}

	return ret;
}

static int32_t s5k_put_toe_info(struct s5_imagectx* ictx)
{
	int32_t ret = 0;
	switch(ictx->dinfo.toe_use_mode)
	{
		case TOE_MODE_DEBUG:
			break;
		case TOE_MODE_CONDUCTOR:
			//get info from conductor.
			ret = cdkt_unregister_volume(ictx);
			break;
		default:
			S5ASSERT(0);
			break;
	}

	return ret;
}

static int32_t s5k_connect_toe (struct s5_imagectx* ictx)
{
	int32_t ret = 0;
	LOG_INFO("Device(%s) start to connect RGE(0x%x:%d).",
		ictx->dinfo.dev_name, ictx->toe_ip, ictx->toe_port);

	if(ictx->socket_fd != NULL)
	{
		kernel_sock_shutdown(ictx->socket_fd, SHUT_RDWR);
		sock_release(ictx->socket_fd);
		ictx->socket_fd = NULL;
	}

update_toe_info:
	//get toe info from conductor or debug mode
	ret = s5k_get_toe_info(ictx);
	if(ret != 0)
	{
		return -BDD_MSG_STATUS_GET_TOE_INFO_FAILED;
	}

	s5k_set_inetaddr(ictx->toe_ip, ictx->toe_port,
		&ictx->toe_server_addr);

	if ((ret = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP,
		&ictx->socket_fd)) < 0)
	{
		LOG_ERROR("Fail to create socket. errorno: %d.", ret);
		return ret;
	}
	else
	{
		ictx->s5k_tcp_tx_msghdr.msg_name = (struct sockaddr *) &ictx->toe_server_addr;
		ictx->s5k_tcp_tx_msghdr.msg_namelen = sizeof(struct sockaddr);
		ictx->s5k_tcp_tx_msghdr.msg_control = NULL;
		ictx->s5k_tcp_tx_msghdr.msg_controllen = 0;
		ictx->s5k_tcp_tx_msghdr.msg_flags = MSG_WAITALL;

		ictx->s5k_tcp_rx_msghdr.msg_name = (struct sockaddr *) &ictx->toe_server_addr;
		ictx->s5k_tcp_rx_msghdr.msg_namelen = sizeof(struct sockaddr);
		ictx->s5k_tcp_rx_msghdr.msg_control = NULL;
		ictx->s5k_tcp_rx_msghdr.msg_controllen = 0;
		ictx->s5k_tcp_rx_msghdr.msg_flags = MSG_WAITALL;
		LOG_INFO("Create socket success.");
	}

#if 0
	if(strlen(ictx->local_ip) >= strlen("1.1.1.1"))
	{
		memset(&localaddr, 0, sizeof(struct sockaddr_in));
		localaddr.sin_family = AF_INET;
		localaddr.sin_addr.s_addr = s5k_inet_addr(ictx->local_ip);
		ret = kernel_bind(ictx->socket_fd, (struct sockaddr*)&localaddr,
			sizeof(struct sockaddr_in));
		if(ret < 0)
		{
			LOG_ERROR("Binding local ip %s failed! ret(%d)", ictx->local_ip, ret);
			sock_release(ictx->socket_fd);
			ictx->socket_fd = NULL;
			return ret;
		}
	}
#endif

	ret = kernel_connect(ictx->socket_fd, (struct sockaddr *) &ictx->toe_server_addr,
		sizeof(struct sockaddr_in), 0);
	if(ret < 0)
	{
		LOG_ERROR("Connect to server(0x%x:%d) Fail, ret(%d).", ictx->toe_ip,
			ictx->toe_port, ret);
		sock_release(ictx->socket_fd);
		ictx->socket_fd = NULL;
		switch(ictx->dinfo.toe_use_mode)
		{
			case TOE_MODE_DEBUG:
				return ret;
				break;
			case TOE_MODE_CONDUCTOR:
				goto update_toe_info;
				break;
			default:
				S5ASSERT(0);
				break;
		}
//		return ret;
	}
	else
	{
		atomic_set(&ictx->dinfo.dstat.sent_to_rge, 0);
		atomic_set(&ictx->dinfo.dstat.recv_from_rge, 0);
		LOG_INFO("Succeed to connect to server(0x%x:%d).", ictx->toe_ip,
			ictx->toe_port);
	}
#if 0
    struct timeval timeout={2,0};//2s

//  seems uselss
	ret = kernel_setsockopt(ictx->socket_fd, SOL_SOCKET, SO_SNDTIMEO,
				(const char*)&timeout, sizeof(struct timeval));
	if (ret < 0)
	{
		LOG_ERROR("s5_imagectx setsockopt kernel_setsockopt failed.");
	}
#endif

	LOG_INFO("Succeed to create receive thread.");

	ictx->reconnect = FALSE;
//	ictx->connect_times ++;

	return 0;
}


static inline void s5k_clear_tidinfo (s5k_tid_info_t * info)
{
	info->bio = NULL;
	info->slba = 0;
	info->bvec = NULL;
	info->start_jiffies = 0;
	info->tid = 0;
	info->bv_cnt = 0;
	info->read_unit = 0;
	info->timeout_count = 0;
	info->retry_count = 0;
	info->finish_id = 0;

	/* DO NOT! set/clear tid_lock_atomic, fifo_lock_atomic in other function.
     * Only can use s5k_test_set_bit() and  s5k_test_clear_bit()
	 */

	/*volatile unsigned long tid_lock_atomic;
	volatile unsigned long fifo_lock_atomic;*/
}

static int32_t s5k_recv_tail (struct s5_imagectx* ictx)
{
	int32_t rc;
	s5_message_tail_t tail;
	struct kvec iov;

	iov.iov_base = &tail;
	iov.iov_len = sizeof(s5_message_tail_t);

	if ((rc = kernel_recvmsg(ictx->socket_fd, &ictx->s5k_tcp_rx_msghdr,
				&iov, 1, iov.iov_len, ictx->s5k_tcp_rx_msghdr.msg_flags)) !=
				sizeof(s5_message_tail_t))
	{
		LOG_ERROR("Failed to receive msg tail, ret(%d).", rc);
		return -1;
	}
	atomic_inc(&ictx->dinfo.dstat.recv_from_rge);

	return 0;
}


static int32_t s5k_recv_kthread(void *data)
{
	int32_t ret = -1;
	struct s5_imagectx* ictx = data;
	struct kvec iov;
   	uint32_t bd_type;
   	uint32_t tid, tid_seq;
   	uint32_t status;
	int32_t rc;
	s5_message_head_t  s5_msg_head;

	allow_signal(SIGKILL);
	set_current_state(TASK_INTERRUPTIBLE);

    while (!kthread_should_stop())
    {

		iov.iov_base = &s5_msg_head;
		iov.iov_len = sizeof(s5_message_head_t);

		if ((rc = kernel_recvmsg(ictx->socket_fd, &ictx->s5k_tcp_rx_msghdr,
			&iov, 1, sizeof(s5_message_head_t), ictx->s5k_tcp_rx_msghdr.msg_flags)) !=
			sizeof(s5_message_head_t))
		{
			LOG_ERROR("Unexpected received count(%d), expected msg head.", rc);
			ictx->reconnect = 1;
			ret = -1;
			goto out;
		}

		tid = TID(s5_msg_head.transaction_id);
		tid_seq = TID_SEQ(s5_msg_head.transaction_id);
        bd_type = s5_msg_head.msg_type;
        status = s5_msg_head.status & 0xff;

		S5ASSERT(tid < TID_DEPTH);

		//LOG_INFO("recv tid %08x status %x bd_type %x old tid %08x\r\n",
			//	s5_msg_head.transaction_id, s5_msg_head.status, bd_type,
			//	ictx->tidinfo[tid].tid);

		if(TID(ictx->tidinfo[tid].tid) != tid)
		{
			//printk(" + ");
			/*LOG_INFO("WARNING: recv tid %08x status %x bd_type %x old tid %08x"
				" connect_times %d \r\n",
				s5_msg_head.transaction_id, s5_msg_head.status, bd_type,
				ictx->tidinfo[tid].tid, ictx->connect_times);
			*/
			/* reconnect的时候， 发生这种情况有可能是合理的， */
		/*	ictx->kill_all_bio = TRUE;
			ret = -1;
			goto out;*/
		}

		if (ictx->tidinfo[tid].slba != s5_msg_head.slba)
		{
			//printk(" - ");

			/*LOG_INFO("WARNING: recv tid %08x status %x bd_type %x old"
				"tid %08x slba %llu - %llu ",
				s5_msg_head.transaction_id, s5_msg_head.status, bd_type,
				ictx->tidinfo[tid].tid, ictx->tidinfo[tid].slba,
				s5_msg_head.slba, ictx->connect_times);
			*/

			/*if (status != MSG_STATUS_DELAY_RETRY)
			{ // RGE BUG
				ictx->kill_all_bio = TRUE;
				ret = -1;
				goto out;
			}*/
		}

		/* rarely happen, protect timeout check between recv kthread */

		while(s5k_test_set_bit(LOCK_TID_BIT, (void *)
			&ictx->tidinfo[tid].tid_lock_atomic) == 1)
		{
			//printk(" . ");
			schedule_timeout_uninterruptible(1);

		}

		switch (bd_type)
		{
			case MSG_TYPE_WRITE_REPLY:
				if (status == MSG_STATUS_DELAY_RETRY)
			    {
			    	//LOG_INFO("wrtie retry tid %d\r\n", tid);

					if (s5k_recv_tail(ictx) == -1)
					{
						LOG_ERROR("Failed to receive msg tail while processing MSG_TYPE_WRITE_REPLY.");
						ictx->reconnect = 1;
						ret = -1;
						goto out;
					}

					if (tid_seq == TID_SEQ(ictx->tidinfo[tid].tid))
					{
						/*if (ictx->tidinfo[tid].retry_count > 0)
							atomic_dec(&ictx->retrying_count);*/
						s5k_retry_io_resend(ictx, tid);
					}
					else
					{
						LOG_WARN("Unexpected tid(0x%x) received.", tid);
					}

				    s5k_test_clear_bit(LOCK_TID_BIT, (void *)
						&ictx->tidinfo[tid].tid_lock_atomic);
                    continue;
             	}
                else
                {
					//LOG_INFO("write reply : tid %d ", tid);

					if (tid_seq != TID_SEQ(ictx->tidinfo[tid].tid))
					{
						s5k_test_clear_bit(LOCK_TID_BIT, (void *)
							&ictx->tidinfo[tid].tid_lock_atomic);

						break;
					}

					if (ictx->tidinfo[tid].bio == NULL)
					{
						LOG_ERROR("Error message received, tid(0x%x) status(0x%x) bd_type(0x%x).",
									s5_msg_head.transaction_id, s5_msg_head.status, bd_type);
						ictx->kill_all_bio = TRUE;
						ret = -1;
						goto out;
					}
					if (++ ictx->bio_finish_cnt[ictx->tidinfo[tid].finish_id] ==
							ictx->tidinfo[tid].bv_cnt)
					{
						s5bd_end_io_acct(ictx, ictx->tidinfo[tid].bio, tid);
						s5k_finish_statistic(ictx, (struct bio *)ictx->tidinfo[tid].bio, status);
                        if(status == MSG_STATUS_OK)
						{
							bio_endio((struct bio *)ictx->tidinfo[tid].bio, 0);
						}
						else
						{
							bio_endio((struct bio *)ictx->tidinfo[tid].bio, -EIO);
							LOG_ERROR("Error msg status(0x%x) while processing MSG_TYPE_WRITE_REPLY.",
								s5_msg_head.status);
						}
						ret = kfifo_in(&ictx->finish_id_generator, &(ictx->tidinfo[tid].finish_id), sizeof(int32_t));
						S5ASSERT(ret == sizeof(int32_t));

					}

					/*if (ictx->tidinfo[tid].retry_count > 0)
						atomic_dec(&ictx->retrying_count);*/

					s5k_clear_tidinfo(&ictx->tidinfo[tid]);
					s5k_test_clear_bit(LOCK_TID_BIT, (void *)
						&ictx->tidinfo[tid].tid_lock_atomic);

					S5ASSERT(sizeof(int32_t) == kfifo_in_spinlocked(&ictx->id_generator,
								&tid, sizeof(int32_t), &ictx->lock_tid));
                }
				break;

			case MSG_TYPE_READ_REPLY:

				if (status == MSG_STATUS_DELAY_RETRY)
                {
			    	//LOG_INFO("read_retry tid %d\r\n", tid);
					if (s5k_recv_tail(ictx) == -1)
					{
						LOG_ERROR("Failed to receive msg tail while processing MSG_TYPE_READ_REPLY.");
						ictx->reconnect = 1;
						ret = -1;
						goto out;
					}

					if (tid_seq == TID_SEQ(ictx->tidinfo[tid].tid))
					{
						/*if (ictx->tidinfo[tid].retry_count > 0)
							atomic_dec(&ictx->retrying_count);*/
						s5k_retry_io_resend(ictx, tid);
					}
                    else
                    {
						LOG_WARN("Unexpected tid(0x%x) received.",
							s5_msg_head.transaction_id);
                    }
					s5k_test_clear_bit(LOCK_TID_BIT, (void *)
						&ictx->tidinfo[tid].tid_lock_atomic);

					continue;
                }
				else
				{
					if (tid_seq != TID_SEQ(ictx->tidinfo[tid].tid))
					{
						
						int data_to_discard;
						iov.iov_base = (char*)discard_buf;
						iov.iov_len = 4096;
						LOG_ERROR("Discarding data for tid(0x%x), %d bytes", s5_msg_head.transaction_id, s5_msg_head.data_len);
						for (data_to_discard = s5_msg_head.data_len; data_to_discard > 0; data_to_discard -= rc)
						{
							if ((rc = kernel_recvmsg(ictx->socket_fd, &ictx->s5k_tcp_rx_msghdr, &iov,
								1, min(data_to_discard, 4096), ictx->s5k_tcp_rx_msghdr.msg_flags)) < 0)
							{
								LOG_ERROR("Failed to receive data, expected 4k, but received %d.", rc);

								s5k_test_clear_bit(LOCK_TID_BIT, (void *)
									&ictx->tidinfo[tid].tid_lock_atomic);

								ictx->reconnect = 1;
								ret = -1;
								goto out;
							}
						}
						s5k_test_clear_bit(LOCK_TID_BIT, (void *)
							&ictx->tidinfo[tid].tid_lock_atomic);

						break;

					}
					else
					{
						int i;
						s5k_tid_info_t * pidinfo = &ictx->tidinfo[tid];
						//LOG_INFO("recv 4096 bytes data for tid:0x%x to %p, size:%d", tid, iov.iov_base, ictx->tidinfo[tid].bvec->bv_len);

						if (s5_msg_head.data_len > 0)
						{
							struct kvec data_iov[pidinfo->bio->bi_vcnt];
							int total_buf_len = 0;
							for (i = 0; i < pidinfo->bio->bi_vcnt; i++)
							{
								data_iov[i].iov_base = page_address(pidinfo->bvec[i].bv_page) + pidinfo->bvec[i].bv_offset;
								data_iov[i].iov_len = pidinfo->bvec[i].bv_len;
								total_buf_len += data_iov[i].iov_len;
							}

							S5ASSERT(total_buf_len == s5_msg_head.data_len);
							if ((rc = kernel_recvmsg(ictx->socket_fd, &ictx->s5k_tcp_rx_msghdr, data_iov,
								pidinfo->bio->bi_vcnt, total_buf_len, ictx->s5k_tcp_rx_msghdr.msg_flags)) != total_buf_len)
							{
								LOG_ERROR("Failed to receive data, expected 4k, but received %d.", rc);

								s5k_test_clear_bit(LOCK_TID_BIT, (void *)
									&ictx->tidinfo[tid].tid_lock_atomic);

								ictx->reconnect = 1;
								ret = -1;
								goto out;
							}
						}

						/* the data never written */
						
						if (((s5_msg_head.data_len) == 0) && (status == MSG_STATUS_OK))
							for (i = 0; i < pidinfo->bio->bi_vcnt; i++)
							{
								memset(page_address(pidinfo->bvec[i].bv_page) + pidinfo->bvec[i].bv_offset, 0, pidinfo->bvec[i].bv_len);
							}
					}
					
					s5bd_end_io_acct(ictx, ictx->tidinfo[tid].bio, tid);

					s5k_finish_statistic(ictx, (struct bio *)ictx->tidinfo[tid].bio, status);
					if (status  == MSG_STATUS_OK)
		            {
						bio_endio((struct bio  * )ictx->tidinfo[tid].bio, 0);
		            }
		            else
					{
						bio_endio((struct bio *) ictx->tidinfo[tid].bio, -EIO);
						LOG_ERROR("End bio with error, tid(0x%x).", tid);
					}
					ret = kfifo_in(&ictx->finish_id_generator, &(ictx->tidinfo[tid].finish_id), sizeof(int32_t));
					S5ASSERT(ret == sizeof(int32_t));
					


		           if(status != MSG_STATUS_OK)
		           {
						LOG_ERROR("Error msg status received, tid(%d)-status(0x%x).",tid,
							s5_msg_head.status);
		           }

				   /*if (ictx->tidinfo[tid].retry_count > 0)
					  atomic_dec(&ictx->retrying_count);*/

				   s5k_clear_tidinfo(&ictx->tidinfo[tid]);
				   s5k_test_clear_bit(LOCK_TID_BIT, (void *)
				       &ictx->tidinfo[tid].tid_lock_atomic);

				   kfifo_in_spinlocked(&ictx->id_generator,
							   &tid, sizeof(int32_t), &ictx->lock_tid);
				}
				break;

			default:
				LOG_ERROR("Error msg type received msg_type(%d) tid(%d).", bd_type, tid);
				S5ASSERT(0);
				ictx->reconnect = 1;
				ret = -1;
				goto out;
		}

		if (s5k_recv_tail(ictx) == -1)
		{
			LOG_ERROR("Failed to receive msg tail.");
			ictx->reconnect = 1;
			ret = -1;
			goto out;
		}
    }

out:
	complete_all(&ictx->recv_kthread_completion);
    return ret;
}

static int32_t s5k_submit_bio(struct s5_imagectx* ictx, struct bio *bio)
{
	uint32_t tid;
	uint64_t t1;
	int32_t finish_id;
	int32_t  idx = 0;
	uint16_t bi_vcnt = bio->bi_vcnt;
	int32_t ret = 0;

	
	S5ASSERT(bi_vcnt != 0);
	t1  = jiffies;

	while (1)
	{
		ret = kfifo_out_spinlocked(&ictx->id_generator, &tid,
				sizeof(int32_t), &ictx->lock_tid);
		if(ret == sizeof(int32_t))
		{
			S5ASSERT(tid < TID_DEPTH);
			idx++;
			break; //alloc only 1 tid for a bio
		}
		else
		{
			schedule_timeout(1);
		}

		

		if((jiffies - t1) > HZ/100)
		{
			return  -2; //timeout, still no tid for use
		}
	}

	ret = kfifo_out(&ictx->finish_id_generator, &finish_id, sizeof(int32_t));
	S5ASSERT(ret == sizeof(int32_t) && finish_id >= 0 && finish_id < TID_DEPTH);
	ictx->bio_finish_cnt[finish_id] = 0;

    if (bio_data_dir(bio))
    {
		S5ASSERT(tid < TID_DEPTH);
		ictx->tidinfo[tid].bio = bio;
		ictx->tidinfo[tid].finish_id = finish_id;
		ictx->tidinfo[tid].tid = tid | s5k_get_tid_seq(ictx);// TID_SEQ((s5k_stamp++)<<16);
		ictx->tidinfo[tid].bv_cnt = 1; //this IO will be send as a whole
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 2)
		ictx->tidinfo[tid].slba = (bio->bi_sector >> 3) ;	//bi_sector 512 bytes per unit,lba 4096 bytes per unit
		ictx->tidinfo[tid].bvec = &bio->bi_io_vec[0];
#else
		ictx->tidinfo[tid].slba = (bio->bi_iter.bi_sector >> 3);	//bi_sector 512 bytes per unit,lba 4096 bytes per unit
		ictx->tidinfo[tid].bvec = &bio->bi_io_vec[0];
#endif
		ictx->tidinfo[tid].start_jiffies = jiffies;
		if(unlikely(ictx->tidinfo[tid].start_jiffies == 0))
		{
			ictx->tidinfo[tid].start_jiffies = 1;
		}
		
		if (s5k_aio_write(ictx, &ictx->tidinfo[tid]) == -1)
			return -1;
		
	}
	else
	{
		
		S5ASSERT(tid < TID_DEPTH);
		ictx->tidinfo[tid].bio = bio;
		ictx->tidinfo[tid].finish_id = finish_id;
		ictx->tidinfo[tid].tid = tid | s5k_get_tid_seq(ictx);//TID_SEQ((s5k_stamp++)<<16);
		ictx->tidinfo[tid].bv_cnt = bi_vcnt;
		//LOG_INFO("IO read tid:0x%x, bv_cnt:%d bi_sector:%lu, bi_idx:%d bi_size:%d", tid, bi_vcnt, bio->bi_iter.bi_sector, bio->bi_iter.bi_idx, bio->bi_iter.bi_size);
		if (bi_vcnt == 1)
			ictx->tidinfo[tid].read_unit = 0;
		else if (bi_vcnt == 2)
			ictx->tidinfo[tid].read_unit = 1;
		else
			ictx->tidinfo[tid].read_unit = 2;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 2)
		ictx->tidinfo[tid].slba = (bio->bi_sector >> 3) ;	//bi_sector 512 bytes per unit,lba 4096 bytes per unit
		ictx->tidinfo[tid].bvec = &bio->bi_io_vec[0];
#else
		ictx->tidinfo[tid].slba = (bio->bi_iter.bi_sector >> 3) ;	//bi_sector 512 bytes per unit,lba 4096 bytes per unit
		ictx->tidinfo[tid].bvec = &bio->bi_io_vec[0];
#endif
		ictx->tidinfo[tid].start_jiffies = jiffies;
		if(unlikely(ictx->tidinfo[tid].start_jiffies == 0))
		{
			ictx->tidinfo[tid].start_jiffies = 1;
		}


		if (s5k_aio_read(ictx, &ictx->tidinfo[tid]) == -1)
			return -1;

	}

	return 0;
}

void s5k_reset_socket(struct s5_imagectx *ictx)
{
	if (ictx->socket_fd != NULL)
	{
		kernel_sock_shutdown(ictx->socket_fd, SHUT_RDWR);
	}
	wait_for_completion(&ictx->recv_kthread_completion);
	if (ictx->socket_fd != NULL)
	{
		sock_release(ictx->socket_fd);
		ictx->socket_fd = NULL;
	}
	ictx->recv_kthread_id = NULL;

}

void s5k_resend_after_reconnect(struct s5_imagectx *ictx)
{
	uint32_t tid;

	/* stop timer and recv kthread, avoid race */

	kfifo_reset(&ictx->timeout_retry_fifo);

	for (tid = 0; tid < TID_DEPTH; tid ++)
	{
		if ((ictx->tidinfo[tid].start_jiffies != 0) &&
			(ictx->tidinfo[tid].bio != NULL))
		{
			s5k_test_clear_bit(LOCK_FIFO_BIT, (void *)
				&ictx->tidinfo[tid].fifo_lock_atomic);

			ictx->tidinfo[tid].timeout_count = 0;
			ictx->tidinfo[tid].start_jiffies = jiffies;
			if(unlikely(ictx->tidinfo[tid].start_jiffies == 0))
			{
				ictx->tidinfo[tid].start_jiffies = 1;
			}
			s5k_timeout_io_resend(ictx, tid);

			LOG_INFO("Device(%s) resend message tid(0x%x) bio(%p) after reconnected.", ictx->dinfo.dev_name, tid,
				ictx->tidinfo[tid].bio);
		}
	}
}

static int32_t s5k_send_kthread (void *arg)
{
	struct s5_imagectx *ictx = (struct s5_imagectx*)arg;
    struct bio *bio;
	uint32_t tid;
	s5k_tid_info_t * pidinfo;
    int32_t rc;
	uint32_t ret;

	while (!kthread_should_stop())
	{
		wait_event_interruptible_timeout (ictx->send_kthread_wq,
			!bio_list_empty(&ictx->send_bio_list) ||
			kfifo_len(&ictx->timeout_retry_fifo), HZ/100);

   		while (1)
		{
			if (unlikely(ictx->kill_all_bio))
			{
				s5k_stop_check_timer(ictx);
				s5k_reset_socket(ictx);
				s5k_bio_err_all(ictx);
				ictx->send_kthread_id = NULL;
				return 0;
			}
			/* reconnect s5 */

			if (unlikely(ictx->reconnect))
			{
				s5k_stop_check_timer(ictx);
				s5k_reset_socket(ictx);
				if(s5k_connect_toe(ictx) < 0)
				{
					ictx->kill_all_bio = TRUE;
					LOG_ERROR("Failed to reconnect RGE(0x%x:%d).",
							ictx->toe_ip, ictx->toe_port);
					break;
				}
				else
				{
					LOG_INFO("Device(%s) succeed to reconnect RGE(0x%x:%d).",
							ictx->dinfo.dev_name, ictx->toe_ip, ictx->toe_port);
					s5k_resend_after_reconnect(ictx);
					s5k_start_check_timer(ictx);
					s5k_start_recv_thread(ictx);
					ictx->reconnect = FALSE;
				}
			}

			/* resend timeout io , 4K unit */

		   while (kfifo_len(&ictx->timeout_retry_fifo))
		   {
             	rc = kfifo_out(&ictx->timeout_retry_fifo, &tid,	sizeof(uint32_t));
             	S5ASSERT(rc == sizeof(uint32_t));
				pidinfo = &ictx->tidinfo[TID(tid)];

				ret = s5k_test_clear_bit(LOCK_FIFO_BIT, (void *)
					&pidinfo->fifo_lock_atomic);

				S5ASSERT(ret == 1);

				if (bio_data_dir(pidinfo->bio))
				{
					rc = s5k_aio_write(ictx, pidinfo);
					if (rc < 0)
						ictx->reconnect = 1;
				}
				else
				{
					rc = s5k_aio_read(ictx, pidinfo);
					if (rc < 0)
						ictx->reconnect = 1;
				}

			/*	if (rc == 0)
					atomic_inc(&ictx->retrying_count);*/
		   }

			/* send new bio */

		 /*if (atomic_read(&ictx->retrying_count) < 5)*/
			{
				if (!bio_list_empty(&ictx->send_bio_list))
				{
					spin_lock (&ictx->lock_bio);
					bio = bio_list_pop(&ictx->send_bio_list);
					spin_unlock(&ictx->lock_bio);

					rc = s5k_submit_bio(ictx, bio);
			    	if (rc == -1)
			   		{
						ictx->reconnect = 1;
						break;
			    	}

					if (rc == -2)
			   		{
						spin_lock(&ictx->lock_bio);
						bio_list_add(&ictx->send_bio_list, bio);
						spin_unlock(&ictx->lock_bio);
			    	}
				}
			}

			if (bio_list_empty(&ictx->send_bio_list) &&
				(kfifo_len(&ictx->timeout_retry_fifo) == 0))
				break;
		}
	}
	ictx->send_kthread_id = NULL;
	return 0;
}

static inline void s5k_fill_msg_head(struct s5_imagectx *ictx, s5_message_head_t * head)
{
	head->magic_num = S5MESSAGE_MAGIC;
	head->image_id = ictx->volume_id;
	//head->user_id = ictx->user_id;
	//head->iops_density = ictx->iops_per_GB;
	//head->snap_seq = ictx->iops_per_GB;
	head->nlba = 1;
	//head->pool_id = 3;
}

static int32_t s5k_aio_write (struct s5_imagectx *ictx,  s5k_tid_info_t * pidinfo)
{
	uint32_t len;
	int32_t rc;
	int i;
	s5_message_tail_t  s5_msg_tail; //useless
	s5_message_head_t  s5_msg_head;
	struct kvec iov[2+pidinfo->bio->bi_vcnt];

	memset(&s5_msg_head, 0, sizeof(s5_message_head_t));
	s5_msg_head.msg_type = MSG_TYPE_WRITE;
	s5_msg_head.transaction_id = pidinfo->tid;
	s5_msg_head.data_len = 0;
	s5_msg_head.slba = pidinfo->slba;

	s5k_fill_msg_head(ictx, &s5_msg_head);

	iov[0].iov_base = &s5_msg_head;
	iov[0].iov_len = sizeof(s5_message_head_t);
	len = iov[0].iov_len;
	for (i = 0; i < pidinfo->bio->bi_vcnt; i++)
	{
		iov[i+1].iov_base = page_address(pidinfo->bvec[i].bv_page) + pidinfo->bvec[i].bv_offset;
		iov[i+1].iov_len = pidinfo->bvec[i].bv_len;
		len += iov[i+1].iov_len;
		s5_msg_head.data_len += iov[i + 1].iov_len;
		//LOG_INFO("bvec[%d], offset=%d, bv_len=%d total_len=%d\n", i, pidinfo->bvec[i].bv_offset, pidinfo->bvec[i].bv_len, len);
	}

	if (s5_msg_head.data_len & 0x0fff)
	{
		LOG_INFO("aio_write error, length not 4k aligned. tid %08x slba %llu imageid %llu total_len=%d\n", pidinfo->tid, pidinfo->slba,
			s5_msg_head.image_id, len);
		S5ASSERT(0 == "length not 4k aligned");
	}
	iov[1 + pidinfo->bio->bi_vcnt].iov_base = &s5_msg_tail;
	iov[1 + pidinfo->bio->bi_vcnt].iov_len = sizeof(s5_message_tail_t);
	len += sizeof(s5_message_tail_t);
//	LOG_INFO("aio_write tid %08x slba %llu imageid %llu total_len=%d\n", pidinfo->tid, pidinfo->slba,
//		s5_msg_head.image_id, len);

	rc = kernel_sendmsg(ictx->socket_fd, &ictx->s5k_tcp_tx_msghdr, iov,
		2 + pidinfo->bio->bi_vcnt, len);

	if (rc > 0)
		S5ASSERT(rc == len);

//	LOG_INFO("aio_write tid %08x sent ok, len=%d rc=%d.\n", pidinfo->tid, len, rc);
	if (likely(rc == len))
	{
		atomic_inc(&ictx->dinfo.dstat.sent_to_rge);
		return 0;
	}
	else
	{
		LOG_ERROR("Failed to send msg tid(%x) slba(%llu) when calling s5k_aio_write ret(%d).",
				pidinfo->tid, pidinfo->slba, rc);
		return -1;
	}
}

static int32_t s5k_aio_read (struct s5_imagectx *ictx,  s5k_tid_info_t * pidinfo)
{
	int32_t rc;
	uint32_t len;
	s5_message_tail_t  s5_msg_tail; //useless
	s5_message_head_t  s5_msg_head;
#define S5_READ_MSG_SEGMENT 2
	struct kvec iov[S5_READ_MSG_SEGMENT];

	memset(&s5_msg_head, 0, sizeof(s5_message_head_t));
	s5_msg_head.msg_type = MSG_TYPE_READ;
	s5_msg_head.transaction_id = pidinfo->tid;
	s5_msg_head.data_len = 0;
	s5_msg_head.slba = pidinfo->slba;
	s5_msg_head.read_unit = pidinfo->read_unit;
	s5k_fill_msg_head(ictx, &s5_msg_head);
	s5_msg_head.nlba = pidinfo->bio->bi_iter.bi_size >> LBA_LENGTH_ORDER;

	iov[0].iov_base = &s5_msg_head;
	iov[0].iov_len = sizeof(s5_message_head_t);
	iov[1].iov_base = &s5_msg_tail;
	iov[1].iov_len = sizeof(s5_message_tail_t);

	len = iov[0].iov_len + iov[1].iov_len;

//	LOG_INFO("aio_read tid %08x slba %d imageid %d\r\n", pidinfo->tid, pidinfo->slba,
	//	s5_msg_head.image_id);

	rc = kernel_sendmsg(ictx->socket_fd, &ictx->s5k_tcp_tx_msghdr, iov,
		S5_READ_MSG_SEGMENT, len);

	if (rc > 0)
		S5ASSERT(rc == len);
//	LOG_INFO("Sent aio_read tid %08x slba %llu imageid %llu rc=%d", pidinfo->tid, pidinfo->slba,
//		s5_msg_head.image_id, rc);

	if (likely(rc == len))
	{
		atomic_inc(&ictx->dinfo.dstat.sent_to_rge);
		return 0;
	}
	else
	{
		LOG_ERROR("Failed to send msg tid(0x%x) slba(%llu) when calling s5k_aio_read ret(%d).",
				pidinfo->tid, pidinfo->slba, rc);
		return -1;
	}
}

/**
 * s5k_test_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It may be reordered on other architectures than x86.
 * It also implies a memory barrier.
 *
 * static inline int s5k_test_set_bit(int nr, volatile unsigned long *addr)
 */

void s5k_bio_err_all(struct s5_imagectx *ictx)
{
	uint32_t tid;
	uint32_t sub_id;

	for (tid = 0; tid < TID_DEPTH; tid ++)
	{
		if (ictx->tidinfo[tid].bio != NULL)
		{
			LOG_WARN("End bio with error, tid(0x%x) bio(%p).", tid, ictx->tidinfo[tid].bio);

			s5k_finish_statistic(ictx, (struct bio *)ictx->tidinfo[tid].bio, -1);
			bio_endio((struct bio *)ictx->tidinfo[tid].bio, -EIO);

			for (sub_id = tid + 1; sub_id < TID_DEPTH; sub_id ++)
			{
				if (ictx->tidinfo[sub_id].bio == ictx->tidinfo[tid].bio)
				{
					LOG_WARN("Clear same bio,  tid(0x%x) bio(%p).", tid, ictx->tidinfo[sub_id].bio);
					ictx->tidinfo[sub_id].bio = NULL;
				}
			}

			ictx->tidinfo[tid].bio = NULL;
		}
	}
}

static void s5k_timeout_io_resend(struct s5_imagectx *ictx, uint32_t tid)
{
	int ret;
	S5ASSERT(tid >= 0 && tid < TID_DEPTH);

	if(s5k_test_set_bit(LOCK_FIFO_BIT, (void *)
		&ictx->tidinfo[tid].fifo_lock_atomic) == 1)
	{
		//LOG_INFO(" ^ ");
		return;
	}

	ictx->tidinfo[tid].tid &= 0x0000ffff;
	ictx->tidinfo[tid].tid |= s5k_get_tid_seq(ictx);//TID_SEQ((s5k_stamp++)<<16);
    ret = kfifo_in_spinlocked(&ictx->timeout_retry_fifo, &tid,
		sizeof(uint32_t), &ictx->lock_retry_timeout);
#if 0
	//S5ASSERT(ret == sizeof(uint32_t));
#else
	if (ret != sizeof(uint32_t))
	{
		LOG_ERROR("Failed to push tid(0x%x) in timeout retry fifo.", tid);
		ictx->kill_all_bio = TRUE;
	}
#endif
}

static void s5k_retry_io_resend(struct s5_imagectx *ictx, uint32_t tid)
{
	int ret;
	S5ASSERT(tid >= 0 && tid < TID_DEPTH);

	if(s5k_test_set_bit(LOCK_FIFO_BIT, (void *)
		&ictx->tidinfo[tid].fifo_lock_atomic) == 1)
		return;

	/*ictx->tidinfo[tid].retry_count ++;*/

    ret = kfifo_in_spinlocked(&ictx->timeout_retry_fifo, &tid,
		sizeof(uint32_t), &ictx->lock_retry_timeout);
    S5ASSERT(ret == sizeof(uint32_t));
}

static void s5k_check_io_timeout(unsigned long param)
{
	uint32_t tid;
	uint32_t recv_timeout_conflict = 0;
    struct s5_imagectx *ictx = (struct s5_imagectx*)param;

	/*printk("retrying %d \r\n", atomic_read(&ictx->retrying_count));*/

	for (tid = 0; tid < TID_DEPTH; tid ++)
	{
		if (s5k_test_set_bit(LOCK_TID_BIT, (void *)
			&ictx->tidinfo[tid].tid_lock_atomic) == 0)
		{
			if ((ictx->tidinfo[tid].start_jiffies != 0) &&
				(ictx->tidinfo[tid].bio != NULL) &&
				time_is_before_jiffies((unsigned long)(ictx->tidinfo[tid].start_jiffies + BIO_RE_SEND_TIMEOUT_HZ)))
//				(jiffies - ictx->tidinfo[tid].start_jiffies) >
//				BIO_RE_SEND_TIMEOUT_HZ))
			{
				s5k_timeout_io_resend(ictx, tid);
				ictx->tidinfo[tid].timeout_count ++;

				/*LOG_INFO("device(%s)  timeout tid %d,  resend count %d ",
					ictx->dev_name, tid,
					ictx->tidinfo[tid].timeout_count);
				printk(" $ ");*/
				if (ictx->tidinfo[tid].timeout_count > RE_SEND_COUNTS_LIMIT)
				{
					s5k_test_clear_bit(LOCK_TID_BIT, (void *)
						&ictx->tidinfo[tid].tid_lock_atomic);

					ictx->reconnect = TRUE;
					wake_up(&ictx->send_kthread_wq);

					LOG_WARN("Device(%s) need to reconnect RGE(0x%x:%d),"
							" for tid(0x%x) timeout %d times.",
							ictx->dinfo.dev_name, ictx->toe_ip, ictx->toe_port,
							tid, RE_SEND_COUNTS_LIMIT);
					return;
				}
			}

			s5k_test_clear_bit(LOCK_TID_BIT, (void *)
				&ictx->tidinfo[tid].tid_lock_atomic);

		}
		else
		{
			++recv_timeout_conflict;
			//printk(" $ ");
		}
	}

	atomic_set(&ictx->dinfo.dstat.recv_timeout_conflict, recv_timeout_conflict);
	wake_up(&ictx->send_kthread_wq);
	mod_timer(&ictx->check_mon, jiffies + CHECK_TIMER_FREQ);
}


