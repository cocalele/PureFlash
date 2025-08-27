/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * This file implements the apis to initialize/release this server, and the functions to handle request messages.
 */

#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/prctl.h>
#include <pthread.h>

#include "pf_server.h"
#include "pf_tcp_connection.h"
#include "pf_connection.h"
#include "pf_poller.h"
#include "pf_errno.h"

#include "pf_utils.h"
#include "pf_main.h"
#include "pf_conf.h"
#include "pf_buffer.h"
#include "pf_flash_store.h"
#include "pf_volume.h"
#include "pf_dispatcher.h"
#include "spdk/env.h"

static void *afs_listen_thread(void *param);
static int server_on_tcp_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* conn, void* cbk_data);

void *afs_listen_thread(void *param)
{
	((PfTcpServer*)param)->listen_proc();

	return NULL;
}


int PfTcpServer::init()
{
	int rc = 0;
	conf_file_t conf=app_context.conf;
	poller_cnt = conf_get_int(conf, "tcp_server", "poller_count", 8, FALSE);
	pollers = new PfPoller[poller_cnt];
	for(int i = 0; i < poller_cnt; i++)
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

void PfTcpServer::listen_proc()
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

int on_tcp_handshake_sent(BufferDescriptor* bd, WcStatus status, PfConnection* conn, void* cbk_data)
{
	int rc = 0;
	delete (PfHandshakeMessage*)bd->buf;
	bd->buf = NULL;//for debug
	delete bd;

	if (status == WcStatus::WC_SUCCESS) {
		if (conn->state == CONN_CLOSING) {
			S5LOG_WARN("Handshake sent but connection in state:%d is to be closed, conn:%s", conn->state, conn->connection_info.c_str());
			conn->state = CONN_OK;//to make close works correctly
			//conn->close();  // will be closed in PfTcpConnection::do_send
			rc = -EINVAL;
			goto release0;
		}
		S5LOG_INFO("Handshake sent OK, conn:%s, io_depth:%d", conn->connection_info.c_str(), conn->io_depth);

		conn->on_work_complete = server_on_tcp_network_done;
		conn->state = CONN_OK;

		for (int i = 0; i < conn->io_depth * 2; i++) {
			auto io = conn->dispatcher->iocb_pool.alloc();
			if (io == NULL)
			{
				S5LOG_ERROR("No enough memory for connection:%s", conn->connection_info.c_str());
				// conn->close(); // will be closed in PfTcpConnection::do_send
				rc = -ENOMEM;
				goto release0;
			}
			io->add_ref();
			conn->add_ref();
			io->conn = conn;
			rc = conn->post_recv(io->cmd_bd);
			if (rc) {
				io->dec_ref();
				S5LOG_ERROR("Failed to post_recv:%d  for rc:%d", i, rc);
				break;
			}
		}
	} else {
		S5LOG_ERROR("Failed send handshake for connection:%s", conn->connection_info.c_str());
		conn->state = CONN_OK;//to make close works correctly
		conn->close(); //this will cause dec_ref, in on_server_conn_close
	}
release0:
	return rc;
}

int on_tcp_handshake_recved(BufferDescriptor* bd, WcStatus status, PfConnection* conn_, void* cbk_data)
{
	int rc = 0;
	PfTcpConnection* conn = (PfTcpConnection*)conn_;
	PfHandshakeMessage* hs_msg = (PfHandshakeMessage*)bd->buf;
	PfVolume* vol;
	S5LOG_INFO("Receive handshake for conn:%s, io_depth:%d", conn->connection_info.c_str(), hs_msg->hsqsize);
	conn->state = CONN_OK;
	hs_msg->hs_result = 0;
	if(hs_msg->vol_id != 0 && (hs_msg->hsqsize > PF_MAX_IO_DEPTH || hs_msg->hsqsize <= 0))
	{
		S5LOG_ERROR("Request io_depth:%d invalid, max allowed:%d", hs_msg->hsqsize, PF_MAX_IO_DEPTH);
		hs_msg->hsqsize = PF_MAX_IO_DEPTH;
		hs_msg->hs_result = EINVAL;
		conn->state = CONN_CLOSING;
		rc = -EINVAL;
		goto release0;
	}
	conn->io_depth = hs_msg->hsqsize;
	bd->data_len = sizeof(PfHandshakeMessage);
	if(hs_msg->vol_id != 0) {
		vol = app_context.get_opened_volume(hs_msg->vol_id);
		if (vol == NULL) {
			S5LOG_ERROR("Request volume:0x%lx not opened", hs_msg->vol_id);
			hs_msg->hs_result = (int16_t)EINVAL;
			conn->state = CONN_CLOSING;
			rc = -EINVAL;
			goto release0;
		}
		conn->srv_vol = vol;
		conn->dispatcher = app_context.get_dispatcher();
	} else if (hs_msg->vol_id != -1ULL) {
		conn->dispatcher = app_context.get_dispatcher();
		S5LOG_DEBUG("get replicating connection: %p(%s), assign to dispatcher:%d", conn, conn->connection_info.c_str(), conn->dispatcher->disp_index);
		conn->srv_vol = NULL;
	}
release0:
	S5LOG_INFO("Reply handshake for conn:%s", conn->connection_info.c_str());
	conn->on_work_complete = on_tcp_handshake_sent;
	conn->post_send(bd);
	return rc;
}
void server_on_conn_close(PfConnection* conn)
{
	S5LOG_INFO("conn:%p, %s closed!", conn, conn->connection_info.c_str());
	conn->dec_ref();
	S5LOG_ERROR("TODO: remove conn:%p from heartbeat checker list", conn);
}
void server_on_tcp_conn_destroy(PfConnection* conn)
{
	S5LOG_INFO("conn:%p, %s destroyed!", conn, conn->connection_info.c_str());
	app_context.remove_connection(conn);
}

static int server_on_tcp_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfTcpConnection* conn = (PfTcpConnection*)_conn;
	//S5LOG_DEBUG("network Tx/Rx done, len:%d, op:%s status:%s", bd->data_len, OpCodeToStr(bd->wr_op), WcStatusToStr(complete_status));
	if (likely(complete_status == WcStatus::WC_SUCCESS)) {

		if (bd->wr_op == WrOpcode::TCP_WR_RECV ) {
			if (bd->data_len == PF_MSG_HEAD_SIZE) {
				//message head received
				struct PfServerIocb *iocb = bd->server_iocb;
				iocb->vol_id = bd->cmd_bd->vol_id;
				iocb->data_bd->data_len = bd->cmd_bd->length;
				if (IS_WRITE_OP(bd->cmd_bd->opcode)) {
					iocb->data_bd->data_len = bd->cmd_bd->length;
					conn->add_ref();
					conn->start_recv(iocb->data_bd); //for write, let's continue to receive data [href:data_recv]
					return 1;
				} else {
					iocb->received_time = now_time_usec();
					iocb->received_time_hz = spdk_get_ticks();
					if (spdk_engine_used())
						((PfSpdkQueue *)(conn->dispatcher->event_queue))->post_event_locked(EVT_IO_REQ, 0, iocb);
					else
						conn->dispatcher->event_queue->post_event(EVT_IO_REQ, 0, iocb); //for read
				}
			} else {
				//data received, this is the continue of above start_recv [href:data_recv]
				PfServerIocb *iocb = bd->server_iocb;
				iocb->received_time = now_time_usec();
				iocb->received_time_hz = spdk_get_ticks();
				if (spdk_engine_used())
					((PfSpdkQueue *)(conn->dispatcher->event_queue))->post_event_locked(EVT_IO_REQ, 0, iocb);
				else
					conn->dispatcher->event_queue->post_event(EVT_IO_REQ, 0, iocb); //for write
			}
		}
		else if (bd->wr_op == WrOpcode::TCP_WR_SEND) {
			//IO complete, start next
			PfServerIocb *iocb = bd->server_iocb;
			if (bd->data_len == sizeof(PfMessageReply) && IS_READ_OP(iocb->cmd_bd->cmd_bd->opcode)
					&& iocb->complete_status == PfMessageStatus::MSG_STATUS_SUCCESS) {
				//message head sent complete
				conn->add_ref(); //data_bd reference to connection
				conn->start_send(iocb->data_bd);
				return 1;
			} else {
				/* no re-send mechanism is using currently, so iocb->ref_count will be zero here.
				 * re-use iocb to avoid frequently free/alloc
				 */
				iocb->re_init();
				//S5LOG_DEBUG("post_rece for a new IO, cmd_bd:%p", iocb->cmd_bd);
				conn->post_recv(iocb->cmd_bd);
			}
		}
		else {
			S5LOG_ERROR("Unknown op code:%d", bd->wr_op);
		}

	} else if (unlikely(complete_status == WC_FLUSH_ERR)) {
		struct PfServerIocb *iocb;
		if (bd->wr_op == TCP_WR_RECV ) {
			if(bd->data_len == PF_MSG_HEAD_SIZE) {
				//message head received
				iocb = bd->server_iocb;
//				S5LOG_DEBUG("Connection closed, io:%p in receiving cmd", iocb);
			} else {
				//data received
				iocb = bd->server_iocb;
//				S5LOG_DEBUG("Connection closed, io:%p in receiving data", iocb);
			}
		} else if (bd->wr_op == TCP_WR_SEND) {
			//IO complete, start next
			iocb = bd->server_iocb;
//			S5LOG_DEBUG("Connection closed, io:%p in sending reply", iocb);
		} else {
			S5LOG_ERROR("Connection closed, with unknown op code:%d", bd->wr_op);
			return 0;
		}
		iocb->dec_ref();//will also call conn->dec_ref
	}
	else {
		S5LOG_ERROR("WR complete in unknown status:%d", complete_status);
		//throw std::logic_error(format_string("%s Not implemented", __FUNCTION__);

	}
	return 0;
}


int PfTcpServer::accept_connection()
{
	int const1 = 1;
	int sz = 1 << 20;
	sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);
	int rc = 0;
	BufferDescriptor *bd;
    char client_ip[INET_ADDRSTRLEN];
	int connfd = accept(server_socket_fd, (sockaddr*)&client_addr, &addr_len);

	if (connfd < 0) {
		S5LOG_ERROR("Failed to accept tcp connection, rc:%d", -errno);
		return -errno;
	}

    if(inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL){
        S5LOG_ERROR("get client ip failed, rc:%d", -errno);
		return -errno;
    }

	PfTcpConnection* conn = new PfTcpConnection(false);
	if (conn == NULL)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc PfTcpConnection");
		goto release1;
	}
	rc = conn->init(connfd, get_best_poller(), PF_MAX_IO_DEPTH, PF_MAX_IO_DEPTH * 2); //recv_q is double of send_q, to avoid RNR error
	if(rc)
	{
		S5LOG_ERROR("Failed to int PfTcpConnection, rc:%d", rc);
		goto release2;
	}

	rc = setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, (char*)&const1, sizeof(int));
	if (rc)
	{
		S5LOG_ERROR("Failed to setsockopt TCP_NODELAY, rc:%d", rc);
	}
	rc = setsockopt(connfd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
	if (rc) {
		S5LOG_ERROR("Failed to set RCVBUF, rc:%d", rc);
	}
	rc = setsockopt(connfd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	if (rc) {
		S5LOG_ERROR("Failed to set SNDBUF, rc:%d", rc);
	}
	conn->state = CONN_INIT;

	bd = new BufferDescriptor();
	if(!bd)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc BufferDescriptor");
		goto release3;
	}
	bd->buf = new PfHandshakeMessage;
	bd->wr_op = TCP_WR_RECV;
	if(!bd->buf)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc PfHandshakeMessage");
		goto release4;
	}
	bd->buf_capacity = sizeof(PfHandshakeMessage);
	bd->data_len = bd->buf_capacity;
	conn->on_work_complete = on_tcp_handshake_recved;
	conn->add_ref(); //decreased in `server_on_conn_close`
	conn->conn_type = TCP_TYPE;
	conn->on_close = server_on_conn_close;
	conn->on_destroy = server_on_tcp_conn_destroy;

	conn->last_heartbeat_time = now_time_usec();
	//app_context.ingoing_connections.insert(conn);
	S5LOG_INFO("add tcp conn ip:%s to heartbeat checker list", client_ip);
	app_context.add_connection(conn);
	rc = conn->post_recv(bd);
	if (rc)
	{
		S5LOG_ERROR("Failed to post_recv for handshake");
		goto release5;
	}
	return 0;
release5:
	delete (PfHandshakeMessage*)bd->buf;
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

PfPoller* PfTcpServer::get_best_poller()
{
	static unsigned int poller_idx = 0;
	poller_idx = (poller_idx + 1)%poller_cnt;
	return &pollers[poller_idx];
}

void PfTcpServer::stop()
{
	pthread_cancel(listen_s5toe_thread);
	pthread_join(listen_s5toe_thread, NULL);

	for(int i=0;i<poller_cnt;i++)
	{
		pollers[i].destroy();
	}
	S5LOG_INFO("TCP server stopped");
}

void server_cron_proc(void)
{
	prctl(PR_SET_NAME, "cron_srv");
	while (1)
	{
		if (sleep(10) != 0)
			return;
		uint64_t now = now_time_usec();
#define CLOSE_TIMEOUT_US 180000000ULL
		std::lock_guard<std::mutex> _l(app_context.conn_map_lock);
		for (auto it = app_context.client_ip_conn_map.begin(); it != app_context.client_ip_conn_map.end(); ++it) {
			PfConnection* c = it->second;

			if(c->state == CONN_CLOSED && now > c->close_time + CLOSE_TIMEOUT_US){
				c->add_ref();//dec on process EVT_FORCE_RELEASE_CONN
				S5LOG_WARN("Connection:%p %s closed but not release", c, c->connection_info.c_str());
				c->dispatcher->event_queue->post_event(EVT_FORCE_RELEASE_CONN, 0, c);
			}

			//TODO: check heartbeat here
			//S5LOG_WARN("Connection:%s  in state:%s, will check heartbeat or last_io_time",
			//	c->connection_info.c_str(), ConnState2Str(c->state));
		}
	}
}
