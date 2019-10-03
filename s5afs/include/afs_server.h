/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * S5afs is short for S5 all flash simulator. It is used as the server role to receive the requests from S5 client
 * This file defines the apis to initialize/release this server, and the functions to handle request messages.
 */

#ifndef __S5D_SRV_TOE__
#define __S5D_SRV_TOE__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include "afs_main.h"
#include "afs_request.h"
#define TCP_PORT_BASE   49162               ///<the value of port base.

/**
 * The data structure toe server
 *
 * It is used to keep the members used to receive client request
 */
typedef struct s5d_srv_toe
{
	PS5SRVSOCKET		socket;    					///< The receiver socket in S5afs part
	PS5CLTSOCKET		socket_clt;					///< The receiver client socket in S5afs part
	char*				listen_ip;					///< Listen ip
	unsigned short      listen_port; 				///< Listen port
	pthread_t			listen_s5toe_thread; 		///< The thread to receive toe msg request
	s5_dlist_head_t   	msg_list_head;				///< Msg list from the client
	int					exitFlag;					///< exit handling stage, and goto accept
} s5d_srv_toe_t;

/**
 * The entry for the message in msg_list_head of s5v_handle_entry
 */
typedef struct s5v_msg_entry
{
	s5_dlist_entry_t 	msg_list_entry;	///< The entry of one msg list
	s5_message_t*		msg;			///< Keep the pointer of original msg
	PS5CLTSOCKET		socket;			///< The client socket of this message
} s5v_msg_entry_t;

/**
 *  The callback function to receive write request.
 *
 *  @param[in]	 sockParam  Client socket.
 *  @param[in]   msg        The write request from client.
 *  @param[in]   param      Not usefull now.
 *  @return      The length of real write.
 *  @retval      >=0 The length of real write.
 *  @retval		 -EINVAL	Any one of input parameter is NULL: invalid
 *  @retval      -ENOENT    No avaliable nodes in afs for write request.
 *  @retval      -ENOMEM    No memory left to allocate variables.
 */
int recv_msg_write(void* sockParam, s5_message_t* msg, void* param);

/**
 *  The callback function to receive read request.
 *
 *  @param[in]   sockParam  Client socket.
 *  @param[in]   msg        The read request from client.
 *  @param[in]   param      Not usefull now.
 *  @return      The length of real read.
 *  @retval      >=0 The length of real read.
 *  @retval      -EINVAL    Any one of input parameter is NULL: invalid
 *  @retval      -ENOENT    No expected nodes in S5afs.
 *  @retval      -ENOMEM    No memory left to allocate variables.
 */
int recv_msg_read(void* sockParam, s5_message_t* msg, void* param);

/**
 *  The callback function to receive delete request.
 *
 *  @param[in]   sockParam  Client socket.
 *  @param[in]   msg        The delelte request from client.
 *  @param[in]   param      Not usefull now.
 *  @return      0			Success.
 *  @retval      -EINVAL    Any one of input parameter is NULL: invalid
 */
int recv_msg_block_delete_request(void* sockParam, s5_message_t* msg, void* param);

/**
 *  The callback function to receive statistic request.
 *
 *  @param[in]   sockParam  Client socket.
 *  @param[in]   msg        The statistic request from client.
 *  @param[in]   param      Not usefull now.
 *  @return      0          Success.
 *  @retval      -EINVAL    Any one of input parameter is NULL: invalid
 */
int recv_msg_s5_stat_request(void* sockParam, s5_message_t* msg, void* param);

/**
 *  Initialize store server.
 *
 *  @param[in]   toe_daemon		The pointer to toedaemon instance. This instance should be malloc in advance, and deleted by using release_s5d_srvtoe.
 *  @return      0          Success.
 *  @retval      -S5_CONF_ERR	When the key can not find from S5 config file.
 *  @retval		 -S5_BIND_ERR	When try to bind listen IP or port failed
 */
int init_store_server(struct toedaemon* toe_daemon);

/**
 *  Release toe server.
 *
 *  @param[in]   toe_daemon     The pointer to toedaemon instance.
 *  @return      0          Success.
 *  @retval      -S5_CONF_ERR   When the key can not find from S5 config file.
 *  @retval      -S5_BIND_ERR   When try to bind listen IP or port failed
 */
int release_store_server(struct toedaemon* toe_daemon);

#ifdef __cplusplus
}
#endif
class S5Poller;

class S5TcpServer
{
public:
	S5Poller* pollers;
	int poller_cnt;
	pthread_t			listen_s5toe_thread; 		///< The thread to receive toe msg request
	int server_socket_fd;
	int init(conf_file_t conf);
	void listen_proc();
	int accept_connection();
private:
	/**
	 * choose the best poller to poll incoming connection.
	 * a round-robin way is used to choose poller in `pollers`
	 */
	S5Poller* get_best_poller();
};
#endif	//__S5D_SRV_TOE__

