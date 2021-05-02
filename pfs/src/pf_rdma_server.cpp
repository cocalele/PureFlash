#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include "pf_server.h"
#include "pf_rdma_connection.h"

#define MAX_DISPATCHER_COUNT    10
#define MAX_REPLICATOR_COUNT    10
extern struct disp_mem_pool* disp_mem_pool[MAX_DISPATCHER_COUNT];
extern struct replicator_mem_pool* rep_mem_pool[MAX_REPLICATOR_COUNT];
extern struct PfRdmaDevContext* global_dev_ctx[4];

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
			server->on_connect_request(&event_copy);
			rdma_ack_cm_event(event);
		}
		else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
		{
			rdma_ack_cm_event(event);
		}
		else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED)
		{
			rdma_ack_cm_event(event);
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
	if(likely(complete_status == WcStatus::RDMA_WC_SUCCESS)) {
		if(bd->wr_op == WrOpcode::RDMA_WR_RECV ) {
			if(bd->data_len == PF_MSG_HEAD_SIZE) {
				//message head received
				struct PfServerIocb *iocb = bd->server_iocb;
				iocb->vol = conn->srv_vol;
				iocb->data_bd->data_len = bd->cmd_bd->length;
				if (bd->cmd_bd->opcode == S5_OP_WRITE || bd->cmd_bd->opcode == S5_OP_REPLICATE_WRITE) {
					iocb->data_bd->data_len = bd->cmd_bd->length;
					conn->add_ref();
					conn->post_read(iocb->data_bd, bd->cmd_bd->buf_addr, bd->cmd_bd->rkey);
					return 1;
				} else {
					iocb->received_time = now_time_usec();
					conn->dispatcher->event_queue.post_event(EVT_IO_REQ, 0, iocb); //for read
				}
			}
			else {
				//data received
				PfServerIocb *iocb = bd->server_iocb;
				iocb->received_time = now_time_usec();
				conn->dispatcher->event_queue.post_event(EVT_IO_REQ, 0, iocb); //for write
			}
		}
		else if(bd->wr_op == WrOpcode::RDMA_WR_SEND){
			//IO complete, start next
			PfServerIocb *iocb = bd->server_iocb;
			if(bd->data_len == sizeof(PfMessageReply) && iocb->cmd_bd->cmd_bd->opcode == PfOpCode::S5_OP_READ) {
				//message head sent complete
				conn->add_ref(); //data_bd reference to connection
				return 1;
			} else {
				iocb->dec_ref();
				iocb = (PfServerIocb *)conn->dispatcher->iocb_pool.alloc(); //alloc new IO
				iocb->conn = conn;
				iocb->add_ref();
//				post_receive(conn, iocb->cmd_bd);
				conn->post_recv(iocb->cmd_bd);
			}
		}
		else {
			S5LOG_ERROR("Unknown op code:%d", bd->wr_op);
		}
	}
	else {
		S5LOG_ERROR("WR complete in unknown status:%d", complete_status);
		//throw std::logic_error(format_string("%s Not implemented", __FUNCTION__);
	}
	return 0;
}

int PfRdmaServer::on_connect_request(struct rdma_cm_event* evt)
{
    int rc = 0;
    int outstanding_read = 0;
    struct ibv_qp_init_attr qp_attr;
    struct rdma_conn_param cm_params;
    struct rdma_cm_id* id = evt->id;
    struct PfHandshakeMessage* hs_msg = (struct PfHandshakeMessage*)evt->param.conn.private_data;
    PfVolume *vol;

    conn = new PfRdmaConnection();
    conn->dev_ctx = build_context(id->verbs);
    if(conn->dev_ctx == NULL)
    {
        S5LOG_ERROR("build_context");
        return -1;
    }

    conn->build_qp_attr(&qp_attr);
    rc = rdma_create_qp(id, conn->dev_ctx->pd, &qp_attr);
    if(rc)
    {
        S5LOG_ERROR("rdma_create_qp, errno:%d", errno);
        return rc;
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
	conn->state = CONN_OK;
	hs_msg->hs_result = 0;
	if(hs_msg->vol_id != 0 && (hs_msg->hsqsize > PF_MAX_IO_DEPTH || hs_msg->hsqsize <= 0))
	{
		S5LOG_ERROR("Request io_depth:%d invalid, max allowed:%d", hs_msg->hsqsize, PF_MAX_IO_DEPTH);
		hs_msg->hsqsize=PF_MAX_IO_DEPTH;
		hs_msg->hs_result = EINVAL;
		conn->state = CONN_CLOSING;
		rc = EINVAL;
		goto release0;
	}
	conn->io_depth=hs_msg->hsqsize;
	if(hs_msg->vol_id != 0) {
		vol = app_context.get_opened_volume(hs_msg->vol_id);
		if (vol == NULL) {
			S5LOG_ERROR("Request volume:0x%lx not opened", hs_msg->vol_id);
			hs_msg->hs_result = (int16_t) EINVAL;
			conn->state = CONN_CLOSING;
			rc = -EINVAL;
			goto release0;
		}
		conn->srv_vol = vol;
		conn->dispatcher = app_context.get_dispatcher(hs_msg->vol_id);
	} else {
		static int rep_disp_id  = 0;
		conn->srv_vol = NULL;
		conn->dispatcher = app_context.disps[rep_disp_id];
		rep_disp_id = (int) ((rep_disp_id+1)%app_context.disps.size());
	}
	conn->on_work_complete = server_on_rdma_network_done;
	for (int i=0; i<conn->io_depth; i++)
	{
		PfServerIocb* iocb = conn->dispatcher->iocb_pool.alloc();
		if (iocb == NULL)
		{
			S5LOG_ERROR("iocb_pool alloc");
			rc = -EINVAL;
			goto release0;
		}
		iocb->add_ref();
		iocb->conn = conn;
		rc = post_receive(conn, iocb->cmd_bd);
		if (rc)
		{
			iocb->dec_ref();
			S5LOG_ERROR("rdma post_receive");
			rc = -EINVAL;
			goto release0;
		}
	}
	memset(&cm_params, 0, sizeof(cm_params));
	outstanding_read = conn->dev_ctx->dev_attr.max_qp_rd_atom;
	if (outstanding_read > 128)
		outstanding_read = 128;
	cm_params.responder_resources = (uint8_t)outstanding_read;
	cm_params.initiator_depth = (uint8_t)outstanding_read;
	cm_params.retry_count = 7;
	cm_params.rnr_retry_count = 7;
	rc = rdma_accept(id, &cm_params);
	if (rc)
	{
		S5LOG_ERROR("rdma accepth, errno:%d", errno);
		goto release0;
	}
	conn->add_ref();
	return 0;
release0:
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
