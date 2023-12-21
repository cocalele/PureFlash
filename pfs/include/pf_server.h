/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
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
#include "pf_rdma_connection.h"
#include <rdma/rdma_cma.h>
#define TCP_PORT_BASE   49162               ///<the value of port base.
#define RDMA_PORT_BASE  49160


class PfPoller;

class PfTcpServer
{
public:
	PfPoller* pollers;
	int poller_cnt;
	pthread_t			listen_s5toe_thread; 		///< The thread to receive toe msg request
	pthread_t			hb_check_conn_thread;       /* use heartbeat check conn state */
	int server_socket_fd;
    std::map<std::string, PfConnection*> client_ip_conn_map; /* ref: ip_id_map */
	int init();
	void listen_proc();
	void hb_check_conn();
	int accept_connection();
	void stop();
private:
	/**
	 * choose the best poller to poll incoming connection.
	 * a round-robin way is used to choose poller in `pollers`
	 */
	PfPoller* get_best_poller();
};

class PfRdmaServer
{
public:
	//PfRdmaConnection *conn;
	PfPoller* poller;
	int init(int port);
	int server_socket_fd;
	pthread_t rdma_listen_t;
	std::map<uintptr_t, PfConnection*> client_ip_conn_map;
	struct rdma_event_channel* ec;
	struct rdma_cm_id* cm_id;
	int on_connect_request(struct rdma_cm_event *evt);
};
#endif	//__S5D_SRV_TOE__

