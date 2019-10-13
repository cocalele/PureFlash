/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * This file implements the apis to initialize/release this server, and the functions to handle request messages.
 */

#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "s5_poller.h"
#include "s5errno.h"
#include "afs_server.h"
#include "s5_utils.h"
#include "afs_main.h"
#include "s5conf.h"
#include "s5_buffer.h"
#include "afs_flash_store.h"
#include "s5_connection.h"
#include "s5_tcp_connection.h"

static void *afs_listen_thread(void *param);

static int init_trays()
{
	int rc = -1;




	return rc;
}

static int release_trays()
{
	return 0;
}

int init_store_server()
{
    int rc = -1;

    // get s5daemon config file
    app_context.conf = conf_open(app_context.conf_file_name.c_str());
    if(!app_context.conf)
    {
        S5LOG_ERROR("Failed to find S5afs conf(%s)", app_context.conf_file_name.c_str());
        rc = -S5_CONF_ERR;
        return rc;
    }

	rc = init_trays();
    if(rc < 0)
    {
    	return rc;
	}

	return rc;
}





static int handle_socket_exception(void* clntSock, void* srv_toe)
{
    return 0;
}

void *afs_listen_thread(void *param)
{
	((S5TcpServer*)param)->listen_proc();

	return NULL;
}


int S5TcpServer::init()
{
	int rc = 0;
	conf_file_t conf=app_context.conf;
	poller_cnt = conf_get_int(conf, "tcp_server", "poller_count", 4, FALSE);
	pollers = new S5Poller[poller_cnt];
	for(int i=0;i<poller_cnt;i++)
	{
		rc = pollers[i].init(format_string("TCP_srv_poll_%d", i).c_str(), 512);
		if (rc != 0)
			S5LOG_FATAL("Failed init TCP pollers[%d], rc:%d", i, rc);
	}
	rc = pthread_create(&listen_s5toe_thread, NULL, afs_listen_thread, this);
	if (rc)
	{
		S5LOG_FATAL("Failed to create TCP listen thread failed rc:%d",rc);
		return rc;
	}
	return rc;
}

void S5TcpServer::listen_proc()
{
	int rc = 0;
	int yes = 1;
	sockaddr_in srv_addr;
	S5LOG_INFO("Init TCP server with IP:<NULL>:%d", TCP_PORT_BASE);
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(TCP_PORT_BASE);

	server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);;
	if (server_socket_fd < 0) {
		rc = -errno;
		S5LOG_FATAL("Failed to create TCP server socket, rc:%d", rc);
		goto release1;
	}
	rc = setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int));
	if (rc != 0)
	{
		rc = -errno;
		S5LOG_ERROR("set SO_REUSEPORT failed, rc:%d", rc);
	}
	rc = setsockopt(server_socket_fd, IPPROTO_TCP, TCP_QUICKACK, &yes, sizeof(int));
	if (rc != 0)
	{
		rc = -errno;
		S5LOG_ERROR("set TCP_QUICKACK failed, rc:%d", rc);
	}
	/* Now bind the host address using bind() call.*/
	if (bind(server_socket_fd, (struct sockaddr *) &srv_addr, sizeof(srv_addr)) < 0)
	{
		rc = -errno;
		S5LOG_FATAL("Failed to bind socket, rc:%d", rc);
		goto release2;
	}


	if (listen(server_socket_fd, 128) < 0)
	{
		rc = -errno;
		S5LOG_FATAL("Failed to listen socket, rc:%d", rc);
		goto release2;
	}

	while (1)
	{
		accept_connection();
	}
	return;
release2:
release1:
	return;
}
int on_tcp_handshake_recved(BufferDescriptor* bd, WcStatus status, S5Connection* conn_, void* cbk_data)
{
	S5TcpConnection* conn = (S5TcpConnection*)conn_;
	s5_handshake_message* hs_msg = (s5_handshake_message*)bd->buf;
	if(hs_msg->hsqsize > MAX_IO_DEPTH)
	{
		hs_msg->hsqsize=MAX_IO_DEPTH;
		hs_msg->hs_result = EINVAL;
		//TODO: schedule a delay call to close this connection
		return -EINVAL;
	}
	hs_msg->hs_result=0;
	conn->io_depth=hs_msg->hsqsize;
	conn->state = CONN_OK;
	//conn->volume = NULL;
	S5ASSERT(0);//TODO: init volume correctly before continue
	//TODO: continue to initialize connection, post receive ...
	return 0;
}
void server_on_conn_close(S5Connection* conn)
{

}
void server_on_conn_destroy(S5Connection* conn)
{

}
int S5TcpServer::accept_connection()
{
	sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);
	int rc = 0;
	BufferDescriptor *bd;
	int connfd = accept(server_socket_fd, (sockaddr*)&client_addr, &addr_len);

	if (connfd < 0) {
		S5LOG_ERROR("Failed to accept tcp connection, rc:%d", -errno);
		return -errno;
	}

	S5TcpConnection* conn = new S5TcpConnection();
	if (conn == NULL)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc S5TcpConnection");
		goto release1;
	}
	rc = conn->init(connfd, get_best_poller(), 128, 128);
	if(rc)
	{
		S5LOG_ERROR("Failed to int S5TcpConnection, rc:%d", rc);
		goto release2;
	}

	conn->state = CONN_INIT;

	bd = new BufferDescriptor();
	if(!bd)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc BufferDescriptor");
		goto release3;
	}
	bd->buf = new s5_handshake_message;
	if(!bd->buf)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc s5_handshake_message");
		goto release4;
	}
	bd->buf_size = sizeof(s5_handshake_message);
	bd->data_len = 0;
	bd->on_work_complete = on_tcp_handshake_recved;
	//add this to debug bad performance in Wdindows driver
	conn->add_ref(); //decreased in `server_on_conn_close`
	conn->role = CONN_ROLE_SERVER;
	conn->transport = TRANSPORT_TCP;
	conn->on_close = server_on_conn_close;
	conn->on_destroy = server_on_conn_destroy;

	rc = conn->post_recv(bd);
	if(!rc)
	{
		S5LOG_ERROR("Failed to post_recv for handshake");
		goto release5;
	}

	conn->last_heartbeat_time = now_time_usec();
	app_context.ingoing_connections.insert(conn);
	return 0;
release5:
	delete (s5_handshake_message*)bd->buf;
release4:
	delete bd;
release3:
	conn->close();
release2:
	delete conn;
release1:
	close(connfd);
	return rc;

	if (rc)
	{
		char* addrstr = inet_ntoa(client_addr.sin_addr);
		S5LOG_ERROR("Failed to create_tcp_server_connection for client:%s, rc:%d", addrstr, -errno);
		return -errno;
	}
	return 0;
}

S5Poller* S5TcpServer::get_best_poller()
{
	static unsigned int poller_idx = 0;
	poller_idx = (poller_idx + 1)%poller_cnt;
	return &pollers[poller_idx];
}
