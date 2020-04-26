/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * S5afs is short for S5 all flash simulator. It is used as the server role to receive the requests from S5 client
 * This file defines the apis to initialize/release this server, and the functions to handle request messages.
 */

#ifndef __S5D_SRV_TOE__
#define __S5D_SRV_TOE__

#include <pthread.h>
#include "pf_main.h"
#define TCP_PORT_BASE   49162               ///<the value of port base.



class S5Poller;

class S5TcpServer
{
public:
	S5Poller* pollers;
	int poller_cnt;
	pthread_t			listen_s5toe_thread; 		///< The thread to receive toe msg request
	int server_socket_fd;
	int init();
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

