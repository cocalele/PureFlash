/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include "pf_server.h"
#include "pf_rdma_connection.h"

#define MAX_DISPATCHER_COUNT    10
#define MAX_REPLICATOR_COUNT    10

static int post_receive(struct PfRdmaConnection *conn, BufferDescriptor *bd)
{
    int rc = 0;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    bd->conn = conn;
    bd->wr_op = RDMA_WR_RECV;

    wr.wr_id = (uint64_t)bd;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uint64_t)bd->cmd_bd;
    sge.length = bd->data_len;
    sge.lkey = bd->mrs[conn->dev_ctx->idx]->lkey;

    rc = ibv_post_recv(conn->rdma_id->qp, &wr, &bad_wr);
    if (rc != 0)
    {
        S5LOG_ERROR("ibv_post_recv failed, errno:%d", errno);
        return rc;
    }
    return 0;
}

static void *rdma_server_event_proc(void* arg)
{
	struct rdma_cm_event* event = NULL;
	PfRdmaServer *server = (PfRdmaServer *)arg;
	while(rdma_get_cm_event(server->ec, &event)==0)
	{
		struct rdma_cm_event event_copy;
		memcpy(&event_copy, event, sizeof(*event));
		if(event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST)
		{
			S5LOG_INFO("get connnect request");
			int rc = server->on_connect_request(&event_copy);
			if(rc ){
				S5LOG_ERROR("on_connect_request failed, rc:%d", rc);
				//rdma_reject(event_copy.id, NULL, 0);
			}
			rdma_ack_cm_event(event);
		}
		else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
		{
			struct rdma_cm_id* id = event->id;
			PfRdmaConnection* conn = (PfRdmaConnection*)id->context;
			S5LOG_INFO("rdma connection established, %s", conn->connection_info.c_str());
			rdma_ack_cm_event(event);
		}
		else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED)
		{
			//todo: close rdma conn
			rdma_ack_cm_event(event);
			struct rdma_cm_id* id = event_copy.id;
			PfRdmaConnection* conn = (PfRdmaConnection *)id->context;
			S5LOG_INFO("get event RDMA_CM_EVENT_DISCONNECTED on conn:%p %s, state:%d ref_count:%d", conn, 
				conn->connection_info.c_str(), conn->state, conn->ref_count);
			conn->close();
			conn->dec_ref(); //added in on_connect_request
		}
		else
		{
			rdma_ack_cm_event(event);
		}
	}
	return NULL;
}

static int server_on_rdma_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfRdmaConnection* conn = (PfRdmaConnection*)_conn;
	int rc = 0;
	if(likely(complete_status == WcStatus::WC_SUCCESS)) {
		if(bd->wr_op == WrOpcode::RDMA_WR_RECV ) {
			if(bd->data_len == PF_MSG_HEAD_SIZE) {
				//message head received
				struct PfServerIocb *iocb = bd->server_iocb;
				iocb->vol_id = bd->cmd_bd->vol_id;
				iocb->data_bd->data_len = bd->cmd_bd->length;
				if (bd->cmd_bd->opcode == S5_OP_WRITE || bd->cmd_bd->opcode == S5_OP_REPLICATE_WRITE) {
					iocb->data_bd->data_len = bd->cmd_bd->length;
					//conn->add_ref();
					//S5LOG_INFO("get %d write", bd->cmd_bd->opcode);
					if((rc = conn->post_read(iocb->data_bd, bd->cmd_bd->buf_addr, bd->cmd_bd->rkey)) != 0) {
						S5LOG_ERROR("Failed call post_read, rc:%d", rc);
						iocb->dec_ref_on_error();
					}
					return 1;
				} else {
					iocb->received_time = now_time_usec();
                                        iocb->received_time_hz = spdk_get_ticks();				
					//S5LOG_INFO("get iocmd!!!!command_id:%d seq:%d", bd->cmd_bd->command_id, bd->cmd_bd->command_seq);
					if (spdk_engine_used())
						((PfSpdkQueue *)(conn->dispatcher->event_queue))->post_event_locked(EVT_IO_REQ, 0, iocb);
					else
						conn->dispatcher->event_queue->post_event(EVT_IO_REQ, 0, iocb); //for read
				}
			}else{
				S5LOG_ERROR("RDMA_WR_RECV unkown data"); //should never reach here
			}
		}
		else if(bd->wr_op == WrOpcode::RDMA_WR_SEND){
			if (bd->data_len == PF_MSG_REPLY_SIZE) {
				//IO complete, start next
				PfServerIocb *iocb = bd->server_iocb;
				iocb->re_init();
				rc = conn->post_recv(iocb->cmd_bd);
				if(unlikely(rc)){
					S5LOG_ERROR("Failed call post_recv, rc:%d", rc);
					iocb->dec_ref_on_error();
					return 0;
				}
			} else {
				S5LOG_ERROR("RDMA_WR_SEND unkonwn data"); //should never reach here
			}
		}
		else if(bd->wr_op == WrOpcode::RDMA_WR_WRITE) {
			//read or recovery_read
			//PfServerIocb *iocb = bd->server_iocb;
            //iocb->dec_ref(); //added in Dispatcher::reply_io_to_client
		} else if (bd->wr_op == WrOpcode::RDMA_WR_READ) {
			//data received
			//conn->dec_ref(); //added at above line, post_read
			PfServerIocb *iocb = bd->server_iocb;
			iocb->received_time = now_time_usec();
                        iocb->received_time_hz = spdk_get_ticks();
			if (spdk_engine_used())
				((PfSpdkQueue *)(conn->dispatcher->event_queue))->post_event_locked(EVT_IO_REQ, 0, iocb);
			else
				conn->dispatcher->event_queue->post_event(EVT_IO_REQ, 0, iocb); //for write
		} else {
			S5LOG_ERROR("Unknown op code:%d", bd->wr_op);
		}
	}
	else {
		if(complete_status != WC_FLUSH_ERR){
			S5LOG_ERROR("WR complete in unexpected status:%d, conn ref_count:%d", complete_status, conn->ref_count);
		}
		else if (bd->wr_op != WrOpcode::RDMA_WR_WRITE) {
			//don't call dec_ref for RDMA write, since each time post_write call in Dispatcher::reply_io_to_client, a post_send 
			//is also called immediately. We rely on the FLUSH_ERROR of Reply SEND to reclaim iocb
			PfServerIocb* iocb = bd->server_iocb;
			iocb->dec_ref_on_error(); //will also call conn->dec_ref
		}
		//S5LOG_DEBUG("FLUSH_ERROR on %p, bd:%p, opcode:%d processed", conn, bd, bd->wr_op);
	}
	return 0;
}
static void remove_from_conn_map(PfConnection* _conn)
{

	app_context.remove_connection(_conn);
}

int PfRdmaServer::on_connect_request(struct rdma_cm_event* evt)
{
    int rc = 0;
    int outstanding_read = 0;
    struct ibv_qp_init_attr qp_attr;
    struct rdma_conn_param cm_params;
    struct rdma_cm_id* id = evt->id;
    struct PfHandshakeMessage* hs_msg = (struct PfHandshakeMessage*)evt->param.conn.private_data;
	PfVolume* vol;
	struct sockaddr * peer_addr = rdma_get_peer_addr(id);
	char* ipstr = inet_ntoa(((struct sockaddr_in*)peer_addr)->sin_addr);
	int resource_count = 0;
	S5LOG_INFO("RDMA connection request:vol_id:0x%lx, from:%s", hs_msg->vol_id, ipstr);

	Cleaner _clean;
	PfRdmaConnection *conn = new PfRdmaConnection();
	if(conn == NULL){
		S5LOG_ERROR("Failed alloc new rdma connection");
		rc = -ENOMEM;
		goto release0;
	}
	_clean.push_back([conn](){
		//conn->close(); //no need to close, since not accept yet
		conn->rdma_id = NULL; //avoid duplicated destroy in destructor. rdma id released by release0 branch
		delete conn;
	});
    conn->dev_ctx = build_context(id->verbs);
    if(conn->dev_ctx == NULL)
    {
        S5LOG_ERROR("Failed to build_context");
        rc = -1;
		goto release0;
    }

	conn->prc_cq_poller_idx = conn->dev_ctx->next_server_cq_poller_idx++;
	//conn->prc_cq_poller_idx = 0;
	if (conn->dev_ctx->next_server_cq_poller_idx >= conn->dev_ctx->cq_poller_cnt)
		conn->dev_ctx->next_server_cq_poller_idx = conn->dev_ctx->client_cq_poller_cnt;
	S5LOG_INFO("poller at cq index:%d", conn->prc_cq_poller_idx);
    conn->build_qp_attr(&qp_attr);
    rc = rdma_create_qp(id, conn->dev_ctx->pd, &qp_attr);
    if(rc)
    {
		rc = -errno;
        S5LOG_ERROR("rdma_create_qp, errno:%d", errno);

		goto release0;
    }
    id->context = conn;
    conn->state = CONN_INIT;
	conn->rdma_id = id;
    if(hs_msg->vol_id != 0 && (hs_msg->hsqsize > PF_MAX_IO_DEPTH || hs_msg->hsqsize <= 0))
    {
        S5LOG_ERROR("Request io_depth:%d invalid, max allowed:%d", hs_msg->hsqsize, PF_MAX_IO_DEPTH);
        hs_msg->hsqsize=PF_MAX_IO_DEPTH;
        hs_msg->hs_result = EINVAL;
        conn->state = CONN_CLOSING;
        rc = -EINVAL;
        goto release0;
    }
	conn->conn_type = RDMA_TYPE;
	conn->state = CONN_OK;
	conn->connection_info = get_rdma_desc(id, false);
	hs_msg->hs_result = 0;
	if(hs_msg->vol_id != 0 && (hs_msg->hsqsize > PF_MAX_IO_DEPTH || hs_msg->hsqsize <= 0))
	{
		S5LOG_ERROR("Request io_depth:%d invalid, max allowed:%d", hs_msg->hsqsize, PF_MAX_IO_DEPTH);
		hs_msg->hsqsize=PF_MAX_IO_DEPTH;
		hs_msg->hs_result = EINVAL;
		conn->state = CONN_CLOSING;
		rc = -EINVAL;
		goto release0;
	}
	conn->io_depth=hs_msg->hsqsize;
	if (hs_msg->vol_id != 0) {
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
		S5LOG_DEBUG("get shared client connection: %p(%s), assign to dispatcher:%d", conn, conn->connection_info.c_str(), conn->dispatcher->disp_index);
		conn->srv_vol = NULL;
	} else {
		static int rep_disp_id  = 0;
		conn->dispatcher = app_context.disps[rep_disp_id];
		rep_disp_id = (int) ((rep_disp_id+1)%app_context.disps.size());
		conn->srv_vol = NULL;
	}
	conn->on_work_complete = server_on_rdma_network_done;
	resource_count = conn->io_depth * 2;
	conn->used_iocb.reserve(resource_count);
	for (int i=0; i< resource_count; i++)
	{
		PfServerIocb* iocb = conn->dispatcher->iocb_pool.alloc();
		if (iocb == NULL)
		{
			S5LOG_ERROR("Failed to alloc iocb_pool from dispatcher:%p", conn->dispatcher);
			rc = -EINVAL;
			goto release0;
		}
		iocb->add_ref();
		iocb->conn = conn;
		conn->add_ref();

		rc = post_receive(conn, iocb->cmd_bd);
		if (rc)
		{
			iocb->dec_ref_on_error();
			S5LOG_ERROR("rdma post_receive");
			rc = -EINVAL;
			goto release0;
		}
		conn->used_iocb.push_back(iocb);
	}
	memset(&cm_params, 0, sizeof(cm_params));
	outstanding_read = conn->dev_ctx->dev_attr.max_qp_rd_atom;
	if (outstanding_read > PF_MAX_IO_DEPTH)
		outstanding_read = PF_MAX_IO_DEPTH;
	cm_params.responder_resources = (uint8_t)outstanding_read;
	cm_params.initiator_depth = (uint8_t)outstanding_read;
	cm_params.retry_count = 7;
	cm_params.rnr_retry_count = 7;
	hs_msg->crqsize = (int16_t)conn->io_depth; //return real iodepth to client
	cm_params.private_data = hs_msg;
	cm_params.private_data_len = sizeof(PfHandshakeMessage);
	rc = rdma_accept(id, &cm_params);
	if (rc)
	{
		rc = -errno;
		S5LOG_ERROR("rdma accept, errno:%d", errno);
		goto release0;
	}
	conn->add_ref(); //dec_ref in rdma_server_event_proc, handling DISCONNECT event
	S5LOG_INFO("accept rdma conn %p :%s ",conn, conn->connection_info.c_str());
	_clean.cancel_all();
	conn->on_destroy= remove_from_conn_map;
	app_context.add_connection(conn);
	return 0;
release0:
	S5LOG_ERROR("reject rdma connection, rc:%d", rc);
	rdma_reject(id, NULL, 0);
	rdma_destroy_qp(id);

	return rc;
}

int PfRdmaServer::init(int port)
{
    int rc;
    struct sockaddr_in listen_addr;
    this->ec = NULL;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons((uint16_t)port);

	S5LOG_INFO("Init RDMA Server with IP:<NULL>:%d", port);
	
    this->ec = rdma_create_event_channel();
    rc = rdma_create_id(this->ec, &this->cm_id, NULL, RDMA_PS_TCP);
    if(rc)
    {
        S5LOG_ERROR("rdma_create_id, errno:%d", errno);
        return rc;
    }
    rc = rdma_bind_addr(this->cm_id, (struct sockaddr*)&listen_addr);
    if(rc)
    {
        S5LOG_ERROR("rdma_bind_addr, errno:%d", errno);
        return rc;
    }
    rc = rdma_listen(this->cm_id, 10);
    if(rc)
    {
        S5LOG_ERROR("rdma_listen, errno:%d", errno);
        return rc;
    }
    rc = pthread_create(&rdma_listen_t, NULL, rdma_server_event_proc, this);
    if(rc)
    {
        S5LOG_ERROR("pthread_create, rc:%d", rc);
        return rc;
    }
    return 0;
}
