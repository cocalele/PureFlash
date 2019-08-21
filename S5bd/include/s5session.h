/**
 * Copyright (C), 2014-2015.
 * @file
 * This is a session with basic utilities of communication and some special functions encapsulated in.
 *
 * Session is designed for s5 client to connect with RGE, and cooperate with RGE to perform flow control in
 * s5 client side. Except for basic utilities of communication, some special features are listed below.
 * @li		Session will split a large block(like 4M) IO request of s5 client into multiple small block sub IO requests.
 * @li		Session maintains a queue(s5io_queue) to cache IO request of s5 client. And then, send out these
 *			requests to RGE according congestion status of transmission. And in this process, a large block IO will
 *			be splitted into multiple small block IO. For example, a 1M block read request, can be splitted into 16
 *			64K block read requests, and sent to RGE separately. Until all these 16 read requests are finished, Session
 *			will notify s5 client the request of 1M block is finished.
 * @li		When processing replies from RGE, Session also needs to package replies to sub IO request. For 64K 
 * 			block read request, RGE will reply 16 4K IO replies. Session is in charge of assembling these small IO replies 
 *			into a large block.
 * @li		Session just tries its best to send requests out, but does not guarantee any process sequence of IO requests.
 *			So s5 client needs to ensure IO sequence before handles them to Session.
 * @li		Session uses transaction id to distinguish relations between parent request and its child request. Session will
 *			use top 10 bits of transaction id for child requests which are splitted from one identical large block IO request.
 *			As a result, s5 client can only use low 22 bits of transaction id to distinguish IO request.
 * @li		To reduce memory usage, Session will buffer from s5 client for data receive, no mater it is read or write request.
 *			So s5 client must ensure data field in message is valid until IO is finished, especially for asynchronous IO.
 * @li		Session will take charge of delay retry situation.
 *
 * Implementation details and work flow.
 * @li		S5 client calls s5session_aio_read or s5session_aio_write to pass pointer of s5message to Session.
 * @li		When Session gets s5message from s5 client, it will push it to s5io_queue. This will be processed in the
 *			thread of s5 client. When s5message is pushed, main thread of Session will be waken up.
 * @li		The main thread is responsible for sending and receiving s5messages. To send a message, it will first
 *			check if there are any IOs in subio_queue to send. If there is no IO waiting to send, it will take all
 *			IOs in s5io_queue into subio_queue if any. If there are IOs in subio_queue waiting to send, it will
 *			call get_next_subio to fetch a splitted IO to send. Large block IOs in subio_queue will be splitted
 *			into small ones when get_next_subio is called. Only the main thread accesses subio_queue.
 * @li		The main thread gets IOs from subio_queue, and then sends them out with socket. After that, IOs will be
 *			pushed into submitted_queue. This process will continue until submitted_queue is full. Here submitted_queue
 *			is a fixed-size queue. Before a IO (large block IOs have been splitted into small ones, and here IO is
 *			splitted sub-io) is sent out, transaction id of it will be modified, top 10-bit of which will be set
 *			with index of its position in submitted_queue.
 * @li		The main thread will call epoll_wait to wait reply from RGE when receving replies. According the transaction
 *			id in the fix-size message head which is received first, the main thread chooses the right IO buffer for
 *			data received.
 * @li		When a reply of a subio arrives, the main thread will dequeue this subio from submitted_queue. And if there
 *			are IOs in subio_queue waiting to send and submitted_queue is not full, the main thread will call get_next_subio
 *			to get a new subio to process. If there is no IO waiting in subio_queue or submitted_queue is full, send process
 *			will be blocked until new IO comes or new reply arrives(in this case, submitted_queue will be full).
 * @li		Until all splitted small block IOs of a large block IO are all finished(with all replies arrive), the IO request in
 *			is finished and will be move to callback_queue waiting to call callback function specified by s5 client.
 * @li		Each time the main thread call get_next_subio to fetch a new subio, if any subio in retry_ready_queue, it will fetch
 *			subio from retry_ready_queue first.
 * @li		When a new subio reply arrives, if it is in MSG_STATUS_OK status, the main thread will remove it from submitted_queue,
 *			and make mark in its parent io(large block io) in subio_queue. If all subios of a parent have arrived, this parent io
 *			will be moved from subio_queue to callback_queue waiting to call callback function specified by s5 client.
 * @li		Reply of subio may be in MSG_STATUS_DELAY_RETRY status, then corresponding subio will be moved to retry_waiting_queue,
 *			and retry thread(id: retry_thread_id) will be waken up. The retry thread is charge of timing for delay retry io. When
 *			delay time is up, corresponding io in retry_waiting_queue will be moved to retry_ready_queue waiting for sending.
 */
#ifndef _S5SESSION_H_
#define _S5SESSION_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <limits.h>
#include "liblfds611.h"
#include "fixed_size_queue.h"
#include "s5socket.h"
#include "s5message.h"
#include "s5utils.h"

#define RGE_IO_DEPTH_ORDER 1						///< the power number of 2 about RGE's IO Depth.
#define RGE_IO_DEPTH (1<<RGE_IO_DEPTH_ORDER)		///< RGE's IO Depth.
#define S5_IO_DEPTH 256								///< S5BD IO Depth.
#define LBA_LENGTH_ORDER 12							///< the power number of 2 about LBA Length.
#define TID_CNTR_CLEAR_MASK	((int32)0xffc000ff)		///< use to clear tid 9~21 bit(1111 1111 1100 0000 0000 0000 1111 1111)

/**
 * Verify LBA_LENGTH_ORDER refer to LBA_LENGTH.
 */
#define RGE_BULK_SIZE 1								///< the max count of LBA  in one RGE IO.
#define CACHE_BLOCK_SIZE (S5_OBJ_LEN / LBA_LENGTH)	///< the count of LBA in one cache block, 1024 x 4k = 4M.
#define	MAX_SUBIO_COUNT	S5_IO_DEPTH*CACHE_BLOCK_SIZE				///< the max count of being handled subio in s5session.
#define MESSAGE_TIMEOUT 5000						///< resend subio after time out the count of millisecond

/**
 * Aio callback function type.
 */
typedef void (*aio_cbk)(void* arg);

/**
 * Configure information for s5session.
 */
typedef struct s5session_conf
{
	volatile int s5_io_depth; 	///< s5-io-depth in s5session handling for s5-io(come from s5bd or s5agent).
	volatile int rge_io_depth;	///< rge-io-depth in RGE(or s5vdriver) which submitted by s5session, the rge-io may be splitted by s5session refrenced by rge_io_max_lbas.
	int rge_io_max_lbas;		///< rge can handle max-block-size,unit is LBA.
} s5session_conf_t;

/**
 * Item managed by s5io queue.
 */
typedef struct s5io_queue_item
{
	s5_message_t* msg;				///< source s5message, s5bd will send it to s5session
	aio_cbk callback;				///< aio callback.
	void* cbk_arg;					///< callback parameter.
	int uncompleted_subio_count;	///< how many sub commands to wait
} s5io_queue_item_t;

/**
 * Item managed by submitted queue.
 */
typedef struct subio
{
	s5_message_t msg;			///< s5message of subio.
	s5_dlist_entry_t* parent_io;		///< a point to list entry, has type s5_dlist_entry<subio_queue_item>
	int replied_lba_count;			///< lba count we have received. a lba is a 4k data slice
	int rcved_status;			///< status code have received in previous 4k slice reply
	int timeout_times;			/// io timeout times, time out 3 times reconect
	volatile int64 start_send_time;			///< the start send time with second from 1970.1.1 to now
	int	index;				///< the location in free_subio.
} subio_t;


/**
 * s5 session object
 *
 * s5 session used 7 queues
 *   Queue					Data Structure			Access Threads						Comments
 *  s5io_queue              s5_dlist_head			s5bd, session_thread				manage user input ios
 *  subio_queue				s5_dlist_head			session_thread						manage splited sub ios
 *  submitted_queue			array					session_thread						manage the sub ios which are already submitted
 *  free_io_positions		fixed_size_queue_t		session_thread						manage free position for submitted_queue
 *  callback_queue			lfds611_queue_state		session_thread, callback_thread		manage the user ios which are replied and finished from RGE
 *  retry_ready_queue		lfds611_queue_state		retry_thread, session_thread		manage the waked up replies in this queue. will be sent to submitted queue. 
 *  timeout_queue			lfds611_queue_state		timeout_thread, session_thread		manage the submitted but timeout IOs. will be sent to submitted queue. 
 */
typedef struct s5_session
{
	// network info
	char server_name[HOST_NAME_MAX];			///< server ip.
	int32	session_index;						///< session index.
	uint16 server_port;							///< server port.
	int socket_fd;          					///< s5session socket fd to recv ,send and perform poll.
	BOOL need_reconnect; 						///< need to reconnect yes or no.

	// sesseion queues
	volatile s5_dlist_head_t s5io_queue;		///< list head of s5io_queue.
	pthread_spinlock_t s5io_queue_lock;			///< lock to protect s5io_queue.
	s5_dlist_head_t subio_queue;                ///< list head of subio_queue, contains subio, from where get_next_subio retrieve data.

	subio_t*    free_subio_queue;               ///< free-subio queue.
    fixed_size_queue_t free_subio_positions;    ///< the queue for free subio's position, can find subio refer to the position.

	subio_t** submitted_queue;                  ///< submitted queue.
    fixed_size_queue_t free_io_positions;       ///< queue for remeber free position of submitted-queue, help to manage submitted queue.

    struct lfds611_queue_state *retry_ready_queue;      ///< ready queue of retry subio, waiting for handling.
    struct lfds611_queue_state *timeout_queue;      ///< time out queue of resend submitted io.

    struct lfds611_queue_state *callback_queue; ///< callback queue.

	// children thread, and thread events
    pthread_t session_thread_id;				///< session thread's id
    pthread_t timeout_thread_id;                  ///< time out thread's id.

    int session_thread_eventfd;					///< session thread event file descriptor. Used to wake up session thread.
    int retry_thread_eventfd;                   ///< retry thread event file descriptor. Used to wake up retry thread.
    int session_timeout_check_eventfd;			///< session thread event file descriptor. Used to wake up session thread to check io timeout.
    
    volatile BOOL exit_flag;    				///< the flag to tell children thread to exit.
	int epoll_fd;                               ///< perform poll.

	
	// spy variables
	volatile int accepted_io_count;             	///< IOs put to s5_io_queue
	volatile int callback_enq_count;				///< the count enqueue to callback queue.
    volatile int retry_ready_count;                 ///< the count of ready to retry.
    volatile int handling_io_count; 				///< all io count handling in session.
    volatile int timeout_io_count;					///< time out io count handling in session.

	// send/receive information
	int index_sending_subio;    	///< sending subio's index.	
	int next_lba;					///< next lba address to use, on a new sub command to be generated, this is a local lba, i.e. lba rang from 0 to msg->nlba	
	s5_dlist_entry_t* current_io;   ///< current user input io. Mext subio will come from it.
	int tid_counter;    			///< subio's counter, bit 8~21 of transaction id, increase when subio send to net.	

	s5_message_t rcv_msg;		///< message to save incoming data, used by do_receive.
	
	int rcv_state;				///< receive state, what we are receiving, RCV_HEAD, RCV_DATA_TAIL.

	int rcved_len;              ///< how many has received.
	int wanted_rcved_len;		///< want received length.

	int sent_len;				///< how many has sent
	int wanted_send_len;		///< want to send length of data.

	BOOL readable;              ///< socket buffer have data to read yes or no.
    BOOL writeable;             ///< socket buffer can send data yes or no.

	// others
	s5session_conf_t*	session_conf;	///< s5session configure
	struct s5_volume_ctx* ictx;	 // parent s5 volume context
} s5_session_t;


/**
 * initialize a s5 session object.
 *
 * @param[in] session 	the object to initialize
 * @param[in] ictx		the volume context
 * @param[in] session_conf 	session configure file
 * @param[in] session_index the session index for multi-replica
 * @return 	0 			on success, negative error code on failure.
 * @retval	-EINVAL 	server_ip is invalid.
 * @retval	-EINVAL 	session_conf is invalid, s5_io_depth,rge_io_depth, rge_io_max_lbas is invalid.
 */
int s5session_init(s5_session_t* session, struct s5_volume_ctx* ictx, s5session_conf_t* session_conf, int32 session_index);

/**
 * destroy a s5 session object.
 *
 * @param[in] session 	the object to initialize
 */
void s5session_destory(s5_session_t* session);

/**
 * perform any kind of read, read, flush_read, cache_find
 *
 * @param[in] session the session object to send this message.
 * @param[in] msg the msg for this read IO. this msg and the data buffer of msg should not
 *             		released until it is completed and callback function is called.
 * @param[in] cbk callback function to call on complete.
 * @param[in] cbk_arg arguments to pass to callback.
 * @return 	0 			on success, negative error code on failure.
 * @retval	-EAGAIN 	count of handling io is bigger than s5_io_depth, should retry again.
 * @retval	-ENOMEM 	can not malloc memory for this io.
 */
int s5session_aio_read(s5_session_t* session, s5_message_t* msg, aio_cbk cbk, void* cbk_arg);

/**
 * perform any kind of write, load_write
 *
 * @param[in] session the session object to send this message
 * @param[in] msg the msg for this read IO. this msg and the data buffer of msg should not
 *             released until it is completed and callback function is called.
 * @param[in] cbk callback function to call on complete
 * @param[in] cbk_arg arguments to pass to callback
 * @return 	0 			on success, negative error code on failure.
 * @retval	-EAGAIN 	count of handling io is bigger than s5_io_depth, should retry again.
 * @retval	-ENOMEM 	can not malloc memory for this io.
 */
int s5session_aio_write(s5_session_t* session, s5_message_t* msg,  aio_cbk cbk, void* cbk_arg);

#ifdef __cplusplus
}
#endif
#endif //_S5SESSION_H_
