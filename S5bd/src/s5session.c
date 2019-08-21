#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <arpa/inet.h>		 // For inet_addr()
#include <netinet/tcp.h>
#include <unistd.h> 		 // For close()
#include <errno.h>
#include <time.h>
#include "s5list.h"
#include "s5session.h"
#include "s5utils.h"
#include "s5imagectx.h"
#include "s5log.h"
#include "spy.h"
#include "disable_warn.h"

/**
 * The times of retry for reconnect
 */
#define	RETRY_CONN_TIMES	2

/**
 * Sleep time before reconnect. Unit: second
 */
#define	RETRY_SLEEP_TIME	1

/**
 * check time out per 2 seconds
 */
#define	TIMEOUT_CHECK_SPAN	2

/**
 * subio max time out times
 */
#define MAX_TIMEOUT_TIMES	3

#define alloc_s5io_queue_item()  ((s5io_queue_item_t*)malloc(sizeof(s5io_queue_item_t)))
#define free_s5io_queue_item(p) free(p)

#define alloc_dlist_entry() ((s5_dlist_entry_t*)malloc(sizeof(s5_dlist_entry_t)))
#define free_dlist_entry(p) free(p)

#define is_read(type) (type == MSG_TYPE_READ)
#define is_write(type) (type == MSG_TYPE_WRITE)

#define SUBMIT_Q_INDEX(tid) (tid >> 22)

//receive state
#define RCV_HEAD 1 //receiving message head
#define RCV_DATA_TAIL 2 //receiving message data and tail

#define KEEPALIVE_INTERVAL 5000 //milliseconds between keepalive

extern int64 event_delta;

subio_t* alloc_subio(s5_session_t * s5session)
{
	subio_t* subio = NULL;
	int64 index = (int64)fsq_dequeue(&s5session->free_subio_positions);
	if(index < 0 || index >= s5session->session_conf->s5_io_depth*CACHE_BLOCK_SIZE)
		return subio;
	else
	{
		subio = s5session->free_subio_queue + index;
		subio->index = (int)index;
		return subio;
	}
}

void free_subio(s5_session_t * s5session, subio_t* subio)
{
	int index = subio->index;
	//LOG_ERROR("+++++++enque index:%d", index);
	fsq_enqueue(&s5session->free_subio_positions, (void*)(int64)index);
}

/**
 * received specified length data, discard that
 */
void rcv_discard(s5_session_t * s5session, int len)
{
	char buf[5000];
	S5LOG_DEBUG("To discard %d bytes", len); //may be timeout, log for debug
	while(len > 0)
	{
		ssize_t rc = recv(s5session->socket_fd, buf, min((size_t)len, sizeof(buf)), MSG_WAITALL);
		if(rc > 0)
			len -= (int)rc;
		else if(rc < 0)
		{
			s5session->need_reconnect = TRUE;
			return ;
		}
	}
	S5LOG_DEBUG("Discard finished");

}

static int64 get_rge_q_depth(s5_session_t* s5s)
{
    return fsq_space(&s5s->free_io_positions);
}

static void retry_submitted_subios(s5_session_t* s5session)
{
    if(get_rge_q_depth(s5session) == 0)
    {
        return;
    }

    int index = 0;
    for(; index < s5session->session_conf->rge_io_depth; index++)
    {
        subio_t* subio = s5session->submitted_queue[index];
        if(subio != 0)
        {
			//add to retry_ready_queue, and resend imediately
			s5_atomic_add(&s5session->retry_ready_count, 1);
			lfds611_queue_enqueue(s5session->retry_ready_queue, subio);
			write(s5session->session_thread_eventfd, &event_delta, sizeof(event_delta));
			s5session->submitted_queue[index] = NULL;
			fsq_enqueue(&s5session->free_io_positions, (void*)(int64)index);

        }
    }
}

static int s5_get_log2(int value)
{
    int x = 0;
    int data = value;
    if(data&(data-1))
        data <<=1;

    while(data > 1)
    {
        data >>= 1;
        x++;
    }
    return x;
}

// Function to fill in address structure given an address and port
static int fillAddr(const char* address, unsigned short port, struct sockaddr_in *addr)
{
	memset(addr, 0, sizeof(*addr));  // Zero out address structure
	addr->sin_family = AF_INET;       // Internet address
	addr->sin_addr.s_addr = inet_addr(address);
	addr->sin_port = htons(port);     // Assign port in network byte order
	return 0;
}


/**
 * move ios in s5io_queue to subio_queue
 * @return count of ios taken from s5io_queue
 */
static inline int  take_io_from_s5io_queue(s5_session_t *s5session)
{
	pthread_spin_lock(&s5session->s5io_queue_lock);
	s5_dlist_entry_t * list = s5session->s5io_queue.list;
	if(list == NULL)
	{
		pthread_spin_unlock(&s5session->s5io_queue_lock);
		return 0;
	}
	int count = s5session->s5io_queue.count;
	s5session->s5io_queue.list = NULL;
	s5session->s5io_queue.count = 0;
	pthread_spin_unlock(&s5session->s5io_queue_lock);

	if(s5session->subio_queue.list == NULL)
	{
		s5session->subio_queue.list = list;
		s5session->subio_queue.count = count;
		s5_dlist_entry_t* entry1 = list;

		s5_dlist_entry_t* p = entry1;
		do{
			p->head = &s5session->subio_queue;
			p = p->next;
		}while(p != entry1);
	}
	else
	{
		//merge two queue, append s5io_queue to tail of subio_queue
		//     head      tail
		//    +---+    +---+ Nb
		// Pa | A |-- -| B |--->
		//<---|   |-- -|   |
		//    +---+    +---+
		//     subio_queue
		//
		//
		//                           head    tail
		//                          +---+   +---+ N2
		//                       P1 | 1 |- -| 2 |--->
		//                      <---|   |- -|   |
		//                          +---+   +---+
		//							s5io_queue
		s5_dlist_entry_t* entryA = s5session->subio_queue.list;
		s5_dlist_entry_t* entryB = entryA->prev;
		s5_dlist_entry_t* entry1 = list;
		s5_dlist_entry_t* entry2 = entry1->prev;

		s5_dlist_entry_t* p = entry1;
		do{
			p->head = &s5session->subio_queue;
			p = p->next;
		}while(p != entry1);

		entryA->prev = entry2;
		entryB->next = entry1;
		entry1->prev = entryB;
		entry2->next = entryA;
		s5session->subio_queue.count += count;
	}
	if(s5session->current_io == NULL)
	{
		s5session->current_io = list;
		s5session->next_lba = 0;
	}
	return count;

};

/**
 * get a subio from timeout io queue
 *
 * @return ptr to a subio on success, NULL if no time out io
 */
static inline subio_t* get_timeout_subio(s5_session_t *s5session)
{
	subio_t* subio = NULL;
	int32 tid = 0;
	while (lfds611_queue_dequeue(s5session->timeout_queue, (void*)&tid))
	{
		subio = s5session->submitted_queue[SUBMIT_Q_INDEX(tid)];
		///avoid case : receive the time out io in submitted_queue before resend it
		///and the timeout io's position in submitted_queue is used by another subio
		///the position may save a new ptr or NULL
		if (subio && tid==subio->msg.head.transaction_id)
		{
			return subio;
		}
	}
	return NULL;
}

static inline BOOL has_more_subio(s5_session_t *s5session)
{
	return s5session->current_io != NULL || take_io_from_s5io_queue(s5session)  > 0;
}

/**
 * get a subio from subioqueue.
 * @return 0 on success, -EAGAIN if no more IO
 *
 */
static inline subio_t* get_next_subio(s5_session_t *s5session)
{
	subio_t* subio = NULL;
	if(lfds611_queue_dequeue(s5session->retry_ready_queue, (void**)&subio))
	{
		if(subio == NULL)
		{
			S5ASSERT(0);
			S5LOG_DEBUG("Dequeue get NULL subio");
		}
		s5_atomic_sub(&s5session->retry_ready_count, 1);
		return subio;
	}

	if(!has_more_subio(s5session) )
	{
		return NULL;
	}
	#if DEBUG_SUBIO_MALLOC
	subio = alloc_subio();
	#else
	subio = alloc_subio(s5session);
	#endif
	s5io_queue_item_t* io = (s5io_queue_item_t*)s5session->current_io->param;
	if(subio && io && io->msg)
	{
		memcpy(&subio->msg.head, &io->msg->head, sizeof(subio->msg.head));
		memcpy(&subio->msg.tail, &io->msg->tail, sizeof(subio->msg.tail));
	}
	else
		S5ASSERT(0);
	subio->msg.data = io->msg->data;
	subio->parent_io = s5session->current_io;
	subio->replied_lba_count = 0;
	subio->rcved_status = MSG_STATUS_OK; //an invalid status
	subio->timeout_times = 0;
	if(is_read(subio->msg.head.msg_type) || is_write(subio->msg.head.msg_type))
	{
		int nlba = min(s5session->session_conf->rge_io_max_lbas, subio->msg.head.nlba - s5session->next_lba);
		subio->msg.head.nlba = nlba;
		subio->msg.head.slba += s5session->next_lba;

		int data_offset = s5session->next_lba<<LBA_LENGTH_ORDER;

		if(is_read(subio->msg.head.msg_type))
		{
			subio->msg.data =  (char*)io->msg->data + data_offset;
			subio->msg.head.data_len = 0;
		}
		else if(data_offset < io->msg->head.data_len)
		{//write with data
			subio->msg.data =  (char*)io->msg->data + data_offset;
			subio->msg.head.data_len = min(io->msg->head.data_len - data_offset, nlba << LBA_LENGTH_ORDER);//LBA_LENGTH 4096
		}
		else
		{//write with data_len = 0
			subio->msg.data =  NULL;
			subio->msg.head.data_len = 0;
		}


		s5session->next_lba += nlba;
	}
	else
	{
		s5session->next_lba += subio->msg.head.nlba;
		//S5ASSERT(io->msg->head->nlba==0);
	}

	//LOG_DEBUG("generate subio from request tid:0x%X, subio_slba:0x%lX nlba:%d", io->msg->head->transaction_id, subio->msg.head.slba, subio->msg.head.nlba);
	if(s5session->next_lba >= io->msg->head.nlba)
	{
		//all have been retrieved, move to next
		s5session->current_io = (s5session->current_io->next == s5session->subio_queue.list) ? NULL : s5session->current_io->next;
		s5session->next_lba = 0;
	}

	return subio;
}

/**
 * move io from s5io_queue to callback queue.
 */
static inline void complete_io(s5_session_t *s5session, s5_dlist_entry_t* io_listentry, int status)
{
	s5io_queue_item_t *ioitem = (s5io_queue_item_t*)io_listentry->param;
	ioitem->msg->head.status = status;
	S5ASSERT(io_listentry->head == &s5session->subio_queue);
	s5list_del_ulc(io_listentry);
	S5ASSERT(s5session->subio_queue.list == NULL || (s5session->subio_queue.list->prev && s5session->subio_queue.list->next));

	int r = lfds611_queue_enqueue(s5session->callback_queue, ioitem);
	s5_atomic_add(&s5session->callback_enq_count, 1);

	if(!r)
		S5LOG_ERROR("Lfds611_queue_enqueue err:%d io_count:%d", r, s5session->handling_io_count);
	S5ASSERT(r);
	free_dlist_entry(io_listentry);
	s5_atomic_sub(&s5session->handling_io_count, 1);
	write(s5session->ictx->callback_thread_eventfd, &event_delta, sizeof(event_delta));

}

static inline void handle_readwrite_reply(s5_session_t *s5session, s5_message_t *msg_reply)
{
	int index = SUBMIT_Q_INDEX(msg_reply->head.transaction_id);
	subio_t* subio = s5session->submitted_queue[index];
	int req_type = msg_reply->head.msg_type-1;

	if(msg_reply->head.status == MSG_STATUS_OK)
	{
		if(msg_reply->head.data_len == 0 && msg_reply->head.nlba != 0 && is_read(req_type))
		{
			memset(msg_reply->data, 0, (size_t)msg_reply->head.nlba<<LBA_LENGTH_ORDER);
		}
	}
	subio->replied_lba_count += msg_reply->head.nlba;
	if(subio->rcved_status == MSG_STATUS_OK)
		subio->rcved_status =  msg_reply->head.status;

	S5ASSERT(subio->replied_lba_count <= subio->msg.head.nlba);
	if(subio->replied_lba_count == subio->msg.head.nlba)
	{
		//LOG_DEBUG("subio complete, tid:0x%X, slba:0x%lX nlba:%d",
		//	subio->msg.head.transaction_id,
		//	subio->msg.head.slba,
		//	subio->msg.head.nlba);
		s5io_queue_item_t *io = (s5io_queue_item_t*)subio->parent_io->param;
		if( -- io->uncompleted_subio_count <= 0) //load write may send a msg, with nlba=0, data_len=0, to indicate no this object
		{
			io->msg->head.msg_type = msg_reply->head.msg_type;
			io->msg->head.obj_ver = msg_reply->head.obj_ver;
			if(io->msg->head.status == MSG_STATUS_OK)
				io->msg->head.status = subio->rcved_status;

			complete_io(s5session, subio->parent_io, io->msg->head.status);
		}

		free_subio(s5session, subio);
		s5session->submitted_queue[index] = NULL;
		fsq_enqueue(&s5session->free_io_positions, (void*)(int64)index);

	}

}

//handle
static inline void dispatch_reply_msg(s5_session_t *s5session, s5_message_t *msg)
{
	S5ASSERT(msg->head.msg_type & 0x01);//handle reply only

	int index = SUBMIT_Q_INDEX(msg->head.transaction_id);
	subio_t* subio = s5session->submitted_queue[index];

	//get the low-24bits as status, high 8bits reserve for ic.
	msg->head.status = msg->head.status & 0x00FFFFFF;
	if((msg->head.status &0xff) == MSG_STATUS_DELAY_RETRY)
	{
		//add to retry_ready_queue, and resend imediately
		s5_atomic_add(&s5session->retry_ready_count, 1);
		lfds611_queue_enqueue(s5session->retry_ready_queue, subio);
		write(s5session->session_thread_eventfd, &event_delta, sizeof(event_delta));
		s5session->submitted_queue[index] = NULL;
		fsq_enqueue(&s5session->free_io_positions, (void*)(int64)index);

		return;
	}

	int req_type = msg->head.msg_type-1;
	if(is_read(req_type) || is_write(req_type))
	{
		handle_readwrite_reply(s5session, msg);
		return;
	}

	//for non-read message, or status error, complete this IO immediately
	s5io_queue_item_t* io = (s5io_queue_item_t*)subio->parent_io->param;
	io->msg->head = msg->head;
	io->msg->tail = msg->tail;
	int32 tidmask_low8bit = 0xff;
	io->msg->head.transaction_id = msg->head.transaction_id & tidmask_low8bit;
	S5LOG_DEBUG("Subio and IO complete, tid:0x%X, slba:0x%lX nlba:%d",
		subio->msg.head.transaction_id,
		subio->msg.head.slba,
		subio->msg.head.nlba);
	S5ASSERT(io->uncompleted_subio_count == 1);
	complete_io(s5session, subio->parent_io, msg->head.status);

	free_subio(s5session, subio);

	s5session->submitted_queue[index] = NULL;
	fsq_enqueue(&s5session->free_io_positions, (void*)(int64)index);

}

/**
 * @return 	0:		wanted data have received,
 *		   -EAGAIN:	no more data, caller should continue to call this function to recv on next POLLIN
 *         <other negative number>: connection lost or other errors, reconnect
 *
 * this function may call 1) pthread_exit to exit session_thread, if found exit_flag is set
 *                        2) exit(-errno) to exit whole process, if EFAULT returned by recv,
 *							 this means invalid buffer address
 */
static inline int rcv_with_error_handle(s5_session_t *s5session)
{

	while(s5session->rcved_len < s5session->wanted_rcved_len)
	{
		ssize_t rc = 0;

		//call different receive function
		if(s5session->rcv_state == RCV_HEAD)
		{
			rc = recv(s5session->socket_fd,
				((char*)&s5session->rcv_msg.head) + s5session->rcved_len,
				(size_t)(s5session->wanted_rcved_len - s5session->rcved_len),
				MSG_DONTWAIT);
		}
		else
		{
			if(s5session->rcved_len < s5session->rcv_msg.head.data_len)
			{
				struct iovec iov[2];
				struct msghdr hdr = {NULL, 0, iov, 2, NULL,0, 0} ;

				iov[0].iov_base = (char*)s5session->rcv_msg.data + s5session->rcved_len;
				iov[0].iov_len = (size_t)(s5session->rcv_msg.head.data_len - s5session->rcved_len);
				iov[1].iov_base = (char*)&s5session->rcv_msg.tail;
				iov[1].iov_len = sizeof(s5session->rcv_msg.tail);

				rc = recvmsg(s5session->socket_fd, &hdr, MSG_DONTWAIT);
			}
			else
			{
				rc = recv(s5session->socket_fd,
						((char*)&s5session->rcv_msg.tail) + sizeof(s5session->rcv_msg.tail) - (s5session->wanted_rcved_len - s5session->rcved_len),
						(size_t)(s5session->wanted_rcved_len - s5session->rcved_len),
						MSG_DONTWAIT);
			}


		}
		if(s5session->exit_flag)
		{
			S5LOG_INFO("Session thread exit from rcv_with_error_handle");
			pthread_exit(NULL);
		}
		if(rc != -1)
			s5session->rcved_len += (int)rc;
		else
		{
			if(errno == EAGAIN)
			{
				s5session->readable = FALSE; //all data has readed, wait next
				return -errno;
			}
			else if(errno == EINTR)
			{
				continue;
			}
			else if(errno == EFAULT)
			{
				S5LOG_FATAL("Recv function return EFAULT, for buffer addr:0x%p, rcv_len:%d\n",
					((char*)&s5session->rcv_msg.head) + s5session->rcved_len,
					s5session->wanted_rcved_len - s5session->rcved_len);
				exit(-errno);
			}
			else
			{
				s5session->readable = FALSE;
				s5session->need_reconnect = TRUE;
				return -errno;
			}
		}
	}

	return 0;
}

static int64 get_time_with_msec(struct timeval *current_time)
{
    gettimeofday(current_time, NULL);
    return (int64)current_time->tv_sec*1000 + current_time->tv_usec/1000;
}

static BOOL is_timeout(int64 time_start, int64 time_end, int64 time_span)
{
	return (time_start + time_span > time_end) ? FALSE : TRUE;
}

static void check_timeout(s5_session_t *s5session)
{
    int i = 0;
    subio_t* subio = NULL;
	struct timeval current_time = {0};
	int64 cur_time = get_time_with_msec(&current_time);
    int rge_io_depth = s5session->session_conf->rge_io_depth;
    for(i = 0; i < rge_io_depth; i++)
    {
		subio = s5session->submitted_queue[i];
		if(subio && is_timeout(subio->start_send_time, cur_time, MESSAGE_TIMEOUT))
		{
			s5_atomic_add(&s5session->timeout_io_count, 1);
    		lfds611_queue_enqueue(s5session->timeout_queue, (void*)(int64)subio->msg.head.transaction_id);
        }
    }
}

static int32 get_tid_counter(s5_session_t *s5session)
{
	if (s5session->tid_counter == 0x3fff)
	{
		///0x3fff = max 14 bit num
		s5session->tid_counter = 0;
		return 0x3fff;
	}
	else
	{
		return s5session->tid_counter++;
	}
}

static inline void do_receive(s5_session_t *s5session)
{
	while(s5session->readable)
	{
		rcv_with_error_handle(s5session);
		if(s5session->wanted_rcved_len == s5session->rcved_len)
		{
			int index, req_type;
			switch(s5session->rcv_state)
			{
			case RCV_HEAD:
				index = SUBMIT_Q_INDEX(s5session->rcv_msg.head.transaction_id);

				if((s5session->submitted_queue[index] == NULL
					|| s5session->rcv_msg.head.transaction_id != s5session->submitted_queue[index]->msg.head.transaction_id
					|| s5session->rcv_msg.head.msg_type - 1 != s5session->submitted_queue[index]->msg.head.msg_type ))
				{
					int discard_len = s5session->rcv_msg.head.data_len + (int)sizeof(s5session->rcv_msg.tail);
					if (s5session->submitted_queue[index])
					{
						S5LOG_ERROR("Tid or message type mismatch tid got: 0x%X(0x%X), type got: %d(%d), discard following %d bytes index(%d)",
							s5session->rcv_msg.head.transaction_id,
	                        s5session->submitted_queue[index]->msg.head.transaction_id,
							s5session->rcv_msg.head.msg_type,
	                        s5session->submitted_queue[index]->msg.head.msg_type,
							discard_len,
	                        index);
					}
					else
					{
						S5LOG_ERROR("Tid or message type mismatch tid got: 0x%X, type got: %d, discard following %d bytes index(%d)",
							s5session->rcv_msg.head.transaction_id,
							s5session->rcv_msg.head.msg_type,
							discard_len,
	                        index);
					}
					rcv_discard(s5session, discard_len); //discard following data
					s5session->wanted_rcved_len = sizeof(s5session->rcv_msg.head);
					s5session->rcved_len = 0;
					continue; //continue to start to receive a new message head
				}

				s5session->rcv_state = RCV_DATA_TAIL;
				s5session->wanted_rcved_len = s5session->rcv_msg.head.data_len + (int)sizeof(s5session->rcv_msg.tail);
				s5session->rcved_len = 0;
				req_type = s5session->rcv_msg.head.msg_type - 1; //
				if(is_read(req_type))
				{
					s5session->rcv_msg.data = (char*)s5session->submitted_queue[index]->msg.data
						+ ((s5session->rcv_msg.head.slba - s5session->submitted_queue[index]->msg.head.slba) << LBA_LENGTH_ORDER);
				}
				else
				{
					S5ASSERT(s5session->rcv_msg.head.data_len == 0);
					s5session->rcv_msg.data = 0;
				}
				break;
			case RCV_DATA_TAIL:
				dispatch_reply_msg(s5session, &s5session->rcv_msg);
				s5session->rcv_state = RCV_HEAD;
				s5session->wanted_rcved_len = sizeof(s5session->rcv_msg.head);
				s5session->rcved_len = 0;
				break;
			}
		}
	}


}

static inline void do_send(s5_session_t *s5session)
{
	struct timeval current_time = {0};
	while(s5session->writeable)
	{
		if(s5session->index_sending_subio >= 0)
		{
			//construct a msghdr to send whole message in one call


			//       |---------want send len-----|
			//       |                           |
			//       +---------+--------+--------+
			//       |  head   | data   | tail   |
			//       +---------+--------+--------+
			//       |             |
			//       |----sent len-|

			subio_t* subio = s5session->submitted_queue[s5session->index_sending_subio];

			struct iovec iov[3];
			struct msghdr hdr = {NULL, 0, iov, 0, NULL,0, 0} ;
			int iov_index = 0;

			int r1 = (int)sizeof(subio->msg.head) - s5session->sent_len;
			r1 = max(0, r1);
			if(r1 > 0)
			{
				iov[iov_index].iov_base = (char*)&subio->msg.head + sizeof(subio->msg.head) - r1;
				iov[iov_index].iov_len = (size_t)r1;
				iov_index ++;
			}

			int r2 =  subio->msg.head.data_len + (int)sizeof(subio->msg.head) - s5session->sent_len - r1;
			r2 = max(0, r2);
			if(r2 > 0)
			{
				iov[iov_index].iov_base = (char*)subio->msg.data + subio->msg.head.data_len - r2;
				iov[iov_index].iov_len = (size_t)r2;
				iov_index ++;
			}

			int r3 = (int)sizeof(subio->msg.tail) + subio->msg.head.data_len + (int)sizeof(subio->msg.head) - s5session->sent_len -r1 -r2;
			r3 = max(0, r3);
			if(r3 > 0)
			{
				iov[iov_index].iov_base = (char*)&subio->msg.tail + sizeof(subio->msg.tail) - r3;
				iov[iov_index].iov_len = (size_t)r3;
				iov_index ++;
			}

			hdr.msg_iovlen = (size_t)iov_index;
			subio->start_send_time = get_time_with_msec(&current_time);
			ssize_t rc = sendmsg(s5session->socket_fd, &hdr, MSG_DONTWAIT);
			if(s5session->exit_flag)
			{
				S5LOG_INFO("Session thread exit from do_send");
				pthread_exit(NULL);
			}

			if(rc != -1)
			{
				s5session->sent_len += (int)rc;
				if(s5session->sent_len >= s5session->wanted_send_len)
				{
					s5session->index_sending_subio = -1; //message finished
				}
			}
			else
			{
				if(errno == EAGAIN)
				{
					s5session->writeable = FALSE; //cann't send more, wait next
					return ;
				}
				else if(errno == EINTR)
				{
					continue;
				}
				else if(errno == EFAULT)
				{
					S5LOG_FATAL("Sendmsg return EFAULT");
					exit(-errno);
				}
				else
				{
					s5session->writeable = FALSE;
					s5session->need_reconnect = TRUE;
					return ;
				}
			}
		}

		if(s5session->index_sending_subio == -1 )
		{//no in sending message

			//get another message to send
			subio_t* subio;
			//get io from time out queue
			if ((subio = get_timeout_subio(s5session)))
			{
				if (++subio->timeout_times >= MAX_TIMEOUT_TIMES)
				{
					S5LOG_DEBUG("WARNING subio tid:0x%X time out for %d times", subio->msg.head.transaction_id, subio->timeout_times);
					s5session->writeable = FALSE;
					s5session->need_reconnect = TRUE;
					subio->timeout_times = 0;
					return ;
				}

				s5session->index_sending_subio = SUBMIT_Q_INDEX(subio->msg.head.transaction_id);
				subio->msg.head.transaction_id &= TID_CNTR_CLEAR_MASK;//clear mid 14 bit
				subio->msg.head.transaction_id |= get_tid_counter(s5session) << 8;
				s5session->wanted_send_len = (int)sizeof(subio->msg.head) + subio->msg.head.data_len + (int)sizeof(subio->msg.tail);
				s5session->sent_len = 0;
				continue;
			}
			//get io from retry queue or take from s5io_Q
			if(!fsq_is_empty(&s5session->free_io_positions) && (subio = get_next_subio(s5session)) != NULL)
			{
				int64 index = (int64)fsq_dequeue(&s5session->free_io_positions);
				s5session->index_sending_subio = (int)index;
				int tidmask = 0x3fffff;
				subio->msg.head.transaction_id = (int32)((index<<22) |    // high 10bit
												 (subio->msg.head.transaction_id & tidmask)) | // low 8 bit
												 get_tid_counter(s5session) << 8;			   // mid 14 bit
				s5session->submitted_queue[index] = subio;
				s5session->wanted_send_len = (int)sizeof(subio->msg.head) + subio->msg.head.data_len + (int)sizeof(subio->msg.tail);
				s5session->sent_len = 0;
				//LOG_DEBUG("submit subio tid:0x%X to position:%d", subio->msg.head.transaction_id, index);
			}
			else
			{//no message, or no queue space, whatever, no data to send
				return ;
			}
		}
	}//end while(writeable)
}

/**
 * establish the connection to rge server, and create an epoll fd
 */
static int connect_server(s5_session_t *s5session)
{
	struct sockaddr_in destAddr;
	int rc = 0;
	int i = 0;

	if(s5session->socket_fd >= 0)
	{
		shutdown(s5session->socket_fd, SHUT_RDWR);
		close(s5session->socket_fd);
		s5session->socket_fd = -1;
	}
	if(s5session->epoll_fd >= 0)
	{
		close(s5session->epoll_fd);
		s5session->epoll_fd = -1;
	}

	rc = fillAddr(s5session->ictx->nic_ip[s5session->session_index], (unsigned short)s5session->ictx->nic_port[s5session->session_index], &destAddr);
	if(rc)
		return rc;//error message has logged in fillAddr
	s5session->socket_fd = socket(AF_INET, SOCK_STREAM, 0);

	int ret = 0;
	// Try to connect to the given port
	for(i = 0; i < RETRY_CONN_TIMES; i++)
	{
		int error=-1, len;
    	len = sizeof(int);
    	struct timeval tm;
    	fd_set set;
    	unsigned long ul = 1;
    	ioctl(s5session->socket_fd, FIONBIO, &ul);  // set sockfd to non-block mode

		if( connect(s5session->socket_fd, (struct sockaddr *) &destAddr, sizeof(destAddr)) == -1)
    	{
        	tm.tv_sec = 5;
        	tm.tv_usec = 0;
        	FD_ZERO(&set);
        	s5_fd_set(s5session->socket_fd, &set);
        	if(select(s5session->socket_fd + 1, NULL, &set, NULL, &tm) > 0)
        	{
            	getsockopt(s5session->socket_fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len);
            	if(error == 0)
            	{
                	ret = 0;
            	}
            	else
            	{
                	ret = -1;
            	}
        	}
        	else
        	{
            	ret = -1;
        	}
    	}
    	else
    	{
        	ret = 0;
    	}

		ul = 0;
		ioctl(s5session->socket_fd, FIONBIO, &ul); // set sockfd to block mode
		if(ret < 0)
		{
			close(s5session->socket_fd);
			if ((s5session->socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    		{
        		S5LOG_ERROR("Failed to resetFd of socket creation failed (socket()%s).", strerror(errno));
    		}

			sleep(RETRY_SLEEP_TIME);
			continue;
		}
		break;
	}

	if (ret)
	{
		//throw SocketException("Connect failed (connect())", true);
		S5LOG_ERROR("Failed connect to: %s port: %d errno: %d %s.", s5session->ictx->nic_ip[s5session->session_index], s5session->ictx->nic_port[s5session->session_index], errno, strerror(errno));
		return ret;
	}
	int flag = 1;
	rc = setsockopt(s5session->socket_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
	if(rc < 0)
	{
		S5LOG_ERROR("S5Session setsockopt TCP_NODELAY failed.");
	}
	s5session->epoll_fd = epoll_create(1);

	struct epoll_event poll_ev = {0};
	poll_ev.events=(__u32)(EPOLLOUT|EPOLLIN|EPOLLPRI|EPOLLERR|EPOLLHUP|EPOLLET|EPOLLRDHUP);
	poll_ev.data.fd=s5session->socket_fd;
	rc=epoll_ctl(s5session->epoll_fd, EPOLL_CTL_ADD, s5session->socket_fd, &poll_ev);
	if(rc)
	{
		S5LOG_ERROR("Failed call epoll_ctl. errno: %d %s.",  errno, strerror(errno));
		return rc;
	}
	poll_ev.events=EPOLLIN|EPOLLPRI|EPOLLERR;
	poll_ev.data.fd=s5session->session_thread_eventfd;
	rc=epoll_ctl(s5session->epoll_fd, EPOLL_CTL_ADD, s5session->session_thread_eventfd, &poll_ev);
	if(rc)
	{
		S5LOG_ERROR("Failed call epoll_ctl. errno: %d %s.",  errno, strerror(errno));
		return rc;
	}

	poll_ev.events=EPOLLIN|EPOLLPRI|EPOLLERR;
	poll_ev.data.fd=s5session->session_timeout_check_eventfd;
	rc=epoll_ctl(s5session->epoll_fd, EPOLL_CTL_ADD, s5session->session_timeout_check_eventfd, &poll_ev);
	if(rc)
	{
		S5LOG_ERROR("Failed call epoll_ctl. errno: %d %s.",  errno, strerror(errno));
		return rc;
	}

	s5session->readable = TRUE;
	s5session->writeable = TRUE;
	s5session->need_reconnect = FALSE;

	s5session->rcv_state = RCV_HEAD; //start receive again
	s5session->wanted_rcved_len = sizeof(s5_message_head_t);
	s5session->rcved_len = 0;

	s5session->index_sending_subio = -1;
	//resend all IOs in submitted queue
	retry_submitted_subios(s5session);
	return rc;

}

static void sig_usr(int signo)      /* argument is signal number */
{
}

static int reopen_volume(s5_session_t * s5session, BOOL need_clean)
{
	struct s5_volume_ctx* ictx = s5session->ictx;
	if(need_clean)
	{
		ictx->nic_ip_blacklist_len[s5session->session_index] = 0;
	}

	safe_strcpy(ictx->nic_ip_blacklist[s5session->session_index][ictx->nic_ip_blacklist_len[s5session->session_index]], s5session->ictx->nic_ip[s5session->session_index], IPV4_ADDR_LEN);
	ictx->nic_ip_blacklist_len[s5session->session_index]++;
	return s5_volumectx_open_volume_to_conductor(ictx, s5session->session_index);
}

static void *timeout_thread_proc(void* arg)
{
	s5_session_t *s5session = (s5_session_t *)arg;
	while (1)
	{
		if (s5session->exit_flag)
		{
			return NULL;
		}

		sleep(TIMEOUT_CHECK_SPAN);
		if (s5_atomic_get(&s5session->handling_io_count) > 0)
		{
			write(s5session->session_timeout_check_eventfd, &event_delta, sizeof(event_delta));//wake up session thread
		}
	}
	return NULL;
}

/**
 * main thread procedure for s5_session
 */
static void *session_thread_proc(void* arg)
{
	s5_session_t *s5session = (s5_session_t *)arg;
	struct sigaction sa={{0}}; //double braces to avoid warning, caused by GCC bug 53119
	sa.sa_handler=sig_usr;//SIG_IGN;
	if(sigaction(SIGUSR1, &sa, NULL) == -1)
	{
		S5LOG_INFO("Failed call sigaction. errno: %d %s.",  errno, strerror(errno));
	}
	if(s5session->socket_fd < 0)
	{
		while(connect_server(s5session))
		{
			int rc = reopen_volume(s5session, FALSE);
			if(rc)
			{
				S5LOG_ERROR("Failed to reopen volume, error=%d", rc);
				return NULL;
			}
		}
	}

	while(1)
	{
		if(s5session->need_reconnect && s5session->exit_flag==FALSE
			&& connect_server(s5session) != 0)
		{
			int	rc = reopen_volume(s5session, TRUE);
			if(rc)
			{
				S5LOG_ERROR("Failed to reopen volume, error=%d", rc);
				return NULL;
			}

			while(connect_server(s5session))
			{
				rc = reopen_volume(s5session, FALSE);
				if(rc)
				{
					S5LOG_ERROR("Failed to reopen volume, error=%d", rc);
					return NULL;
				}
			}
		}

		if(s5session->exit_flag)
			return NULL;

		do_receive(s5session);
		do_send(s5session);

		if(s5session->need_reconnect)
			continue;
		struct epoll_event rev[2];
		int nfds = epoll_wait(s5session->epoll_fd, rev, 2, KEEPALIVE_INTERVAL);
		if(s5session->exit_flag)
			return NULL;
		if(nfds == -1)
		{
			//interrupted, no message in
		}
		else if(nfds == 0)
		{
			//time out, send a keepalive
		}

		int i = 0;
		for(i=0;i<nfds;i++)
		{
			if(rev[i].data.fd == s5session->socket_fd)
			{
				if(rev[i].events & (EPOLLERR|EPOLLHUP|EPOLLRDHUP))
				{
					//there's error, resetup connect socket
					s5session->need_reconnect = TRUE;
				}
				if(rev[i].events & (EPOLLIN|EPOLLPRI))
				{
					//data in, receive data
					s5session->readable=TRUE;
				}
				if(rev[i].events & (EPOLLOUT))
				{
					//data in, receive data
					s5session->writeable=TRUE;
				}
			}
			else if(rev[i].data.fd == s5session->session_thread_eventfd)
			{
				int64 r;
				read(s5session->session_thread_eventfd, &r, sizeof(r));
			}
			else if(rev[i].data.fd == s5session->session_timeout_check_eventfd)
			{
				int64 r;
				check_timeout(s5session);
				read(s5session->session_timeout_check_eventfd, &r, sizeof(r));
			}
		}
	}
	return NULL;
}

int s5session_init(s5_session_t* s5session,
				   struct s5_volume_ctx* ictx,
				   s5session_conf_t* session_conf,
				   int32 session_index)
{
	if(strlen(ictx->nic_ip[session_index]) > HOST_NAME_MAX-1) //an IP has at most 15 char
	{
		S5LOG_ERROR("Host name length exceeds limits %d, %s", HOST_NAME_MAX, ictx->nic_ip[session_index]);
		return -EINVAL;
	}

	if(ictx->nic_port[session_index] < 0)
	{
		S5LOG_ERROR("Nic port(%u) should not be negtive.", ictx->nic_port[session_index]);
		return -EINVAL;
	}

	if(!session_conf ||
		session_conf->s5_io_depth <= 0 ||
		session_conf->rge_io_depth <= 0 ||
		session_conf->rge_io_max_lbas < 1)
	{
		S5LOG_ERROR("S5session init's param(conf) is invalid.");
		return -EINVAL;
	}

	memset(s5session, 0, sizeof(s5_session_t));
	s5session->ictx = ictx;
	s5session->need_reconnect = TRUE;
	s5session->session_index = session_index;
	s5session->session_conf = (s5session_conf_t*)malloc(sizeof(s5session_conf_t));
	if(!s5session->session_conf)
	{
		S5LOG_ERROR("Failed to malloc.");
		return -ENOMEM;
	}

	memcpy(s5session->session_conf, session_conf, sizeof(s5session_conf_t));
	s5session->exit_flag=FALSE;
	pthread_spin_init(&s5session->s5io_queue_lock, 0);

	s5list_init_head((ps5_dlist_head_t)&s5session->s5io_queue);
	s5list_init_head(&s5session->subio_queue);
	//sem_init(&s5session->callback_sem, 0, 0);
	int s5_io_depth,rge_io_depth,rge_io_max_lbas, max_subio_cnt;
	s5_io_depth = s5session->session_conf->s5_io_depth;
	rge_io_depth = s5session->session_conf->rge_io_depth;
	rge_io_max_lbas = s5session->session_conf->rge_io_max_lbas;
	S5LOG_INFO("S5Session conf, s5_io_depth(%d) rge_io_depth(%d) rge_io_max_lbas(%d)"
		, s5_io_depth, rge_io_depth, rge_io_max_lbas);
	lfds611_queue_new(&s5session->callback_queue, (lfds611_atom_t)s5_io_depth );
	fsq_init(&s5session->free_io_positions, s5_get_log2(rge_io_depth));
    s5session->submitted_queue = (subio_t**)malloc(sizeof(subio_t*)*(size_t)rge_io_depth);
	if(!s5session->submitted_queue)
	{
		S5LOG_ERROR("Failed to malloc.");
		return -ENOMEM;
	}

	int i;
	for(i=0;i<rge_io_depth;i++)
    {
		fsq_enqueue(&s5session->free_io_positions, (void*)(int64)i);//all positions free to use
        s5session->submitted_queue[i] = NULL;
    }

	max_subio_cnt = s5_io_depth * CACHE_BLOCK_SIZE / rge_io_max_lbas;
	lfds611_queue_new(&s5session->retry_ready_queue, (lfds611_atom_t)max_subio_cnt);
	lfds611_queue_new(&s5session->timeout_queue, (lfds611_atom_t)rge_io_depth);
	s5session->free_subio_queue = (subio_t*)malloc(sizeof(subio_t)*(size_t)max_subio_cnt);
		if(!s5session->free_subio_queue)
	{
		S5LOG_ERROR("Failed to malloc.");
		return -ENOMEM;
	}
	fsq_init(&s5session->free_subio_positions, s5_get_log2(max_subio_cnt));
	int j;
	for(j=0; j<max_subio_cnt; j++)
		fsq_enqueue(&s5session->free_subio_positions, (void*)(int64)j);//all position free to use

	s5session->socket_fd = -1;
	s5session->epoll_fd = -1;
	s5session->readable = TRUE;
	s5session->writeable = TRUE;
	s5session->need_reconnect = TRUE;
	s5session->index_sending_subio=-1;
	s5session->session_thread_eventfd = eventfd(0, EFD_NONBLOCK);
	s5session->retry_thread_eventfd = eventfd(0, 0);
	s5session->session_timeout_check_eventfd = eventfd(0, EFD_NONBLOCK);

	int rc = 0;
	while((rc = connect_server(s5session)) != 0)
	{
		rc = reopen_volume(s5session, FALSE);
		if(rc)
		{
			S5LOG_ERROR("Failed to reopen volume, error=%d", rc);
        		return rc;
		}
	}
	if(rc != 0)
	{
		return rc;
	}

	rc = pthread_create(&s5session->session_thread_id, NULL, session_thread_proc, s5session);
	if(rc)
	{
		S5LOG_ERROR("Failed call pthread_create. errno: %d %s.",  errno, strerror(errno));
		return rc;
	}

	S5LOG_INFO("S5Session created, server: %s:%d", s5session->ictx->nic_ip[s5session->session_index], s5session->ictx->nic_port[s5session->session_index]);
	s5_atomic_set(&s5session->handling_io_count, 0);

	rc = pthread_create(&s5session->timeout_thread_id, NULL, timeout_thread_proc, s5session);
	if(rc)
	{
		S5LOG_ERROR("Failed call pthread_create. errno: %d %s.",  errno, strerror(errno));
		return rc;
	}
	//initialized by another place
	s5_atomic_set(&s5session->timeout_io_count, 0);

	// register spy variables
	char text_name[1024];
	char text_comment[1024];

	snprintf(text_name, 1024, "s_accepted_io_%d", session_index);
	snprintf(text_comment, 1024, "Total IO has been accepted for session %d", session_index);
    spy_register_variable(text_name, (void*)&s5session->accepted_io_count, vt_int32, text_comment);

	snprintf(text_name, 1024, "s_callback_enq_%d", session_index);
	snprintf(text_comment, 1024, "Total IO pushed into callback queue for session %d", session_index);
	spy_register_variable(text_name, (void*)&s5session->callback_enq_count, vt_int32, text_comment);

    snprintf(text_name, 1024, "s_s5_io_q_depth_%d", session_index);
    snprintf(text_comment, 1024, "S5 IO queue depth for session %d", session_index);
	spy_register_variable(text_name, (void*)&s5session->s5io_queue.count, vt_int32, text_comment);

    snprintf(text_name, 1024, "s_subio_q_depth_%d", session_index);
    snprintf(text_comment, 1024, "Sub IO queue depth for session %d", session_index);
	spy_register_variable(text_name, (void*)&s5session->subio_queue.count, vt_int32, text_comment);

    snprintf(text_name, 1024, "s_rege_q_depth_%d", session_index);
    snprintf(text_comment, 1024, "RGE IO queue depth for session %d", session_index);
	spy_register_property_getter(text_name, (property_getter)get_rge_q_depth, s5session, vt_prop_int64, text_comment);

	snprintf(text_name, 1024, "s_retry_ready_%d", session_index);
    snprintf(text_comment, 1024, "Retry-Ready queue depth for session %d", session_index);
	spy_register_variable(text_name, (void*)&s5session->retry_ready_count, vt_int32, text_comment);

	snprintf(text_name, 1024, "s_handling_%d", session_index);
    snprintf(text_comment, 1024, "IOs in processing session %d", session_index);
	spy_register_variable(text_name, (void*)&s5session->handling_io_count, vt_int32, text_comment);

	snprintf(text_name, 1024, "timeout_cnt_%d", session_index);
    snprintf(text_comment, 1024, "time out io count handling in session %d", session_index);
	spy_register_variable(text_name, (void*)&s5session->timeout_io_count, vt_int32, text_comment);

	return rc;

}
void s5session_destory(s5_session_t* s5session)
{
	s5session->exit_flag=TRUE;
	if(s5session->session_thread_id)
	{
		write(s5session->session_timeout_check_eventfd, &event_delta, sizeof(event_delta));
		pthread_join(s5session->session_thread_id, NULL);
	}

	if(s5session->timeout_thread_id)
	{
		exit_thread(s5session->timeout_thread_id);
	}

	if(s5session->epoll_fd >= 0)
		close(s5session->epoll_fd);
	if(s5session->socket_fd >= 0)
		close(s5session->socket_fd);
	if(s5session->session_thread_eventfd >= 0)
		close(s5session->session_thread_eventfd);
	if(s5session->retry_thread_eventfd >= 0)
		close(s5session->retry_thread_eventfd);
	if(s5session->session_timeout_check_eventfd >= 0)
		close(s5session->session_timeout_check_eventfd);

	char text_name[1024];
	int session_index = s5session->session_index;

	snprintf(text_name, 1024, "s_accepted_io_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "s_callback_enq_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "s_s5_io_q_depth_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "s_subio_q_depth_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "s_rege_q_depth_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "s_retry_ready_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "s_handling_%d", session_index);
    spy_unregister(text_name);

    snprintf(text_name, 1024, "timeout_cnt_%d", session_index);
    spy_unregister(text_name);

	pthread_spin_destroy(&s5session->s5io_queue_lock);
	lfds611_queue_delete( s5session->callback_queue, NULL, NULL );
	lfds611_queue_delete( s5session->retry_ready_queue, NULL, NULL );
	lfds611_queue_delete( s5session->timeout_queue, NULL, NULL );

    fsq_destory(&s5session->free_io_positions);
	fsq_destory(&s5session->free_subio_positions);
	free(s5session->free_subio_queue);
	free(s5session->session_conf);
    free(s5session->submitted_queue);
}

static int push_s5io(s5_session_t* s5session, s5_message_t* msg, aio_cbk cbk, void* cbk_arg)
{
	S5ASSERT((msg->head.transaction_id >> 22) == 0);

	s5io_queue_item_t *io = alloc_s5io_queue_item();
	if(!io)
	{
		return -ENOMEM;
	}
	io->msg = msg;
	io->callback=cbk;
	io->cbk_arg=cbk_arg;
	int rge_io_max_lbas = s5session->session_conf->rge_io_max_lbas;
	if(is_read(msg->head.msg_type) || is_write(msg->head.msg_type))
	{
		io->uncompleted_subio_count = (msg->head.nlba + rge_io_max_lbas -1) / rge_io_max_lbas;
	}
	else
	{
		S5ASSERT(0);
	}

	ps5_dlist_entry_t element = alloc_dlist_entry();;
	if(!element)
	{
		free_s5io_queue_item(io);
		return -ENOMEM;
	}

	s5_atomic_add(&s5session->handling_io_count, 1);
	element->param = io;
	element->head = NULL;

	pthread_spin_lock(&s5session->s5io_queue_lock);
	int rc = s5list_pushtail_ulc(element, (ps5_dlist_head_t)&s5session->s5io_queue);
	pthread_spin_unlock(&s5session->s5io_queue_lock);
	S5ASSERT(rc == 0);
	write(s5session->session_thread_eventfd, &event_delta, sizeof(event_delta));//wake up session thread
	s5_atomic_add(&s5session->accepted_io_count,1);
	return 0;
}

int s5session_aio_read(s5_session_t* s5session, s5_message_t* msg, aio_cbk cbk, void* cbk_arg)
{
	return push_s5io(s5session, msg, cbk, cbk_arg);
}

int s5session_aio_write(s5_session_t* s5session, s5_message_t* msg,  aio_cbk cbk, void* cbk_arg)
{
	return push_s5io(s5session, msg, cbk, cbk_arg);
}

