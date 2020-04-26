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

#include "pf_tcp_connection.h"
#include "pf_connection.h"
#include "pf_poller.h"
#include "pf_errno.h"
#include "pf_server.h"
#include "pf_utils.h"
#include "pf_main.h"
#include "pf_conf.h"
#include "pf_buffer.h"
#include "pf_flash_store.h"
#include "pf_volume.h"

static void *afs_listen_thread(void *param);
static int server_on_work_complete(BufferDescriptor* bd, WcStatus complete_status, S5Connection* conn, void* cbk_data);

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

int on_tcp_handshake_sent(BufferDescriptor* bd, WcStatus status, S5Connection* conn, void* cbk_data)
{
	int rc = 0;
	delete (pf_handshake_message*)bd->buf;
	bd->buf = NULL;//for debug
	delete bd;

	if(status == WcStatus::TCP_WC_SUCCESS)
	{
		if(conn->state == CONN_CLOSING)
		{
			S5LOG_WARN("Handshake sent but connection in state:%d is to be closed, conn:%s", conn->state, conn->connection_info.c_str());
			conn->state = CONN_OK;//to make close works correctly
			conn->close();
			goto release0;
		}
		S5LOG_INFO("Handshake sent OK, conn:%s", conn->connection_info.c_str());

		conn->on_work_complete = server_on_work_complete;
		conn->state = CONN_OK;

		for(int i=0;i<conn->io_depth*2;i++)
		{
			BufferDescriptor* cmd_bd = conn->cmd_pool.alloc();
			if(cmd_bd == NULL)
			{
				S5LOG_ERROR("No enough memory for connection:%s", conn->connection_info.c_str());
				conn->close();
				rc = -ENOMEM;
				goto release0;
			}
			rc = conn->post_recv(cmd_bd);
			if(rc)
			{
				S5LOG_ERROR("Failed to post_recv  for rc:%d", rc);
			}
		}
	}
	else
	{
		S5LOG_ERROR("Failed send handshake for connection:%s", conn->connection_info.c_str());
		conn->state = CONN_OK;//to make close works correctly
		conn->close(); //this will cause dec_ref, in on_server_conn_close
	}
release0:
	return rc;
}

int on_tcp_handshake_recved(BufferDescriptor* bd, WcStatus status, S5Connection* conn_, void* cbk_data)
{
	int rc = 0;
	S5Volume * vol;
	S5TcpConnection* conn = (S5TcpConnection*)conn_;
	pf_handshake_message* hs_msg = (pf_handshake_message*)bd->buf;
	S5LOG_INFO("Receive handshake for conn:%s", conn->connection_info.c_str());
	conn->state = CONN_OK;
	hs_msg->hs_result = 0;
	if(hs_msg->hsqsize > MAX_IO_DEPTH || hs_msg->hsqsize <= 0)
	{
		S5LOG_ERROR("Request io_depth:%d invalid, max allowed:%d", hs_msg->hsqsize, MAX_IO_DEPTH);
		hs_msg->hsqsize=MAX_IO_DEPTH;
		hs_msg->hs_result = EINVAL;
		conn->state = CONN_CLOSING;
		rc = -EINVAL;
	}
	conn->io_depth=hs_msg->hsqsize;
	bd->data_len = sizeof(pf_handshake_message);
	vol = app_context.get_opened_volume(hs_msg->vol_id);
	if(vol == NULL)
	{
		S5LOG_ERROR("Request volume:0x%lx not opened", hs_msg->vol_id);
		hs_msg->hs_result = (int16_t) EINVAL;
		conn->state = CONN_CLOSING;
		rc = -EINVAL;
	}
	conn->ulp_data = vol;
	rc = conn->init_mempools();
	if(rc != 0)
	{
		S5LOG_ERROR("No enough memory to accept connection, volume:%s, conn:%s", vol->name, conn->connection_info.c_str());
		hs_msg->hs_result = (int16_t)-rc; //return a positive value
		conn->state = CONN_CLOSING;
		rc = -EINVAL;
	}
	//conn->send_q.init("send_q", conn->io_depth, TRUE);
	//conn->recv_q.init(conn->io_depth*2);
release0:
	S5LOG_INFO("Reply handshake for conn:%s", conn->connection_info.c_str());
	conn->on_work_complete = on_tcp_handshake_sent;
	conn->post_send(bd);
	return rc;
}
void server_on_conn_close(S5Connection* conn)
{
	S5LOG_INFO("conn:%s closed!", conn->connection_info.c_str());
	conn->dec_ref();
}
void server_on_conn_destroy(S5Connection* conn)
{
	S5LOG_INFO("conn:%s destroyed!", conn->connection_info.c_str());
	S5LOG_ERROR("TODO: remove conn from heartbeat checker list");
	//app_context.ingoing_connections.remove(conn);
}

static int server_on_work_complete(BufferDescriptor* bd, WcStatus complete_status, S5Connection* conn, void* cbk_data)
{
	throw std::logic_error("Not implemented");
	return 0;
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
	rc = conn->init(connfd, get_best_poller(), MAX_IO_DEPTH, MAX_IO_DEPTH*2); //recv_q is double of send_q, to avoid RNR error
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
	bd->buf = new pf_handshake_message;
	bd->wr_op = TCP_WR_RECV;
	if(!bd->buf)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc pf_handshake_message");
		goto release4;
	}
	bd->buf_size = sizeof(pf_handshake_message);
	bd->data_len = bd->buf_size;
	conn->on_work_complete = on_tcp_handshake_recved;
	conn->add_ref(); //decreased in `server_on_conn_close`
	conn->role = CONN_ROLE_SERVER;
	conn->transport = TRANSPORT_TCP;
	conn->on_close = server_on_conn_close;
	conn->on_destroy = server_on_conn_destroy;
	rc = conn->post_recv(bd);
	if(rc)
	{
		S5LOG_ERROR("Failed to post_recv for handshake");
		goto release5;
	}

	conn->last_heartbeat_time = now_time_usec();
	//app_context.ingoing_connections.insert(conn);
	S5LOG_ERROR("TODO: add to heartbead checker list");
	return 0;
release5:
	delete (pf_handshake_message*)bd->buf;
release4:
	delete bd;
release3:
	conn->close();
release2:
	delete conn;
release1:
	close(connfd);

	if (rc)
	{
		char* addrstr = inet_ntoa(client_addr.sin_addr);
		S5LOG_ERROR("Failed to create_tcp_server_connection for client:%s, rc:%d", addrstr, rc);
	}
	return rc;

}

S5Poller* S5TcpServer::get_best_poller()
{
	static unsigned int poller_idx = 0;
	poller_idx = (poller_idx + 1)%poller_cnt;
	return &pollers[poller_idx];
}
