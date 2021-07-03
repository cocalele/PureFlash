#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
//#include "rdma/rdma_cma.h"
//#include "pf_main.h"
//#include "pf_server.h"
#include "pf_rdma_connection.h"

#define RDMA_RESOLVE_ROUTE_TIMEOUT_MS 100

#define MAX_DISPATCHER_COUNT	10
#define MAX_REPLICATOR_COUNT	10
#define DEFAULT_MAX_MR		64
struct disp_mem_pool* disp_mem_pool[MAX_DISPATCHER_COUNT]={0};
struct replicator_mem_pool* rep_mem_pool[MAX_REPLICATOR_COUNT]={0};
BufferPool* recovery_bd_pool;
struct PfRdmaDevContext* global_dev_ctx[4];
pthread_mutex_t global_dev_lock;

int on_addr_resolved(struct rdma_cm_id* id)
{
    int rc = rdma_resolve_route(id, RDMA_RESOLVE_ROUTE_TIMEOUT_MS);
    if(rc)
    {
        S5LOG_ERROR("rdma_resolve_route failed, errno:%d", errno);
        return rc;
    }
    return 0;
}

static int register_rdma_mem_pool(BufferPool *pool, struct ibv_pd *pd, int idx, int access_mode)
{
    if (pd == NULL)
    {
	S5LOG_ERROR("pd is NULL");
        return -1;
    }
    S5LOG_DEBUG("register_rdma_mem_pool, idx;%d", idx);
    if (pool->mrs[idx] != NULL)
    {
	S5LOG_ERROR("pool->mrs[%d] is not NULL", idx);
        return -EEXIST;
    }
    struct ibv_mr* mr = pool->mrs[idx] = ibv_reg_mr(pd, pool->data_buf, ((size_t)pool->buf_size)*pool->buf_count, access_mode);
    if (mr == NULL)
    {
	S5LOG_ERROR("ibv_reg_mr failed, idx;%d, errno:%d", idx, errno);
        return -errno;
    }
    for (int i=0; i<pool->buf_count; i++)
    {
        pool->data_bds[i].mrs[idx] = mr;
    }
    return 0;
}

static int register_disp_context_buf(struct ibv_pd* pd, int idx)
{
	int rc = 0;
	for(int i=0; i<MAX_DISPATCHER_COUNT; i++){
		rc = 0;
		if (disp_mem_pool[i] == 0)
			break;
		struct disp_mem_pool* mem_pool = disp_mem_pool[i];
		rc = register_rdma_mem_pool(&mem_pool->cmd_pool, pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
		if (rc)
		{
			S5LOG_ERROR("register_rdma_mem_pool for disp cmd_pool failed, rc:%d", rc);
			return rc;
		}
		rc = register_rdma_mem_pool(&mem_pool->data_pool, pd, idx,
		                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
		if (rc)
		{
			S5LOG_ERROR("register_rdma_mem_pool for disp data_pool failed, rc:%d", rc);
			return rc;
		}
		rc = register_rdma_mem_pool(&mem_pool->reply_pool, pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
		if (rc)
		{
			S5LOG_ERROR("register_rdma_mem_pool for disp reply_pool failed, rc:%d", rc);
			return rc;
		}
		S5LOG_DEBUG("register_dispatcher_mem i:%d done", i);
	}
	return 0;
}

static int register_rep_context_buf(struct ibv_pd* pd, int idx)
{
	int rc;
	for(int i=0; i<MAX_REPLICATOR_COUNT; i++){
		rc = 0;
		if(rep_mem_pool[i] == 0)
			break;
		struct replicator_mem_pool* mem_pool = rep_mem_pool[i];
		rc = register_rdma_mem_pool(&mem_pool->cmd_pool, pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
		if (rc)
		{
			S5LOG_ERROR("register_rdma_mem_pool for replicator cmd_pool failed, rc:%d", rc);
			return rc;
		}
		rc = register_rdma_mem_pool(&mem_pool->reply_pool, pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
		if (rc)
		{
			S5LOG_ERROR("register_rdma_mem_pool for replicator reply_pool failed, rc:%d", rc);
			return rc;
		}
		S5LOG_DEBUG("register_rep_mem i:%d done", i);
	}
	rc = register_rdma_mem_pool(recovery_bd_pool, pd, idx,
	                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
	if (rc)
	{
		S5LOG_ERROR("register_rdma_mem_pool for recovery_io_bd_pool failed, rc:%d", rc);
		return rc;
	}
	return 0;
}

#define MAX_WC_CNT 256
static void *cq_poller_proc(void *arg_)
{
    struct PfRdmaDevContext* dev_ctx = (struct PfRdmaDevContext*)arg_;
    struct ibv_cq *cq;
    struct ibv_wc wc[MAX_WC_CNT];
    void *cq_ctx;
    int n;
    ibv_get_cq_event(dev_ctx->comp_channel, &cq, &cq_ctx);
    ibv_ack_cq_events(cq, 1);
    ibv_req_notify_cq(cq, 0);
    while((n = ibv_poll_cq(cq, MAX_WC_CNT, wc)))
    {   
        for(int i=0; i<n; i++)
        {   
            struct BufferDescriptor* msg = (struct BufferDescriptor*)wc[i].wr_id;
            if(msg == NULL)
            {
		S5LOG_WARN("msg is NULL, continue");
                continue;
            }
            struct PfRdmaConnection* conn = (struct PfRdmaConnection *)msg->conn;
            if(wc[i].status != IBV_WC_SUCCESS){
            	S5LOG_WARN("wc[%d].status != IBV_WC_SUCCESS, wc.status:%d, %s", i, wc[i].status, ibv_wc_status_str(wc[i].status));
            }
            if (likely(conn->on_work_complete))
            {
                conn->on_work_complete(msg, WcStatus::RDMA_WC_SUCCESS, conn, NULL);
            }
        }
    }
    return NULL;
}

static void on_rdma_cq_event(int fd, uint32_t events, void *arg)
{
    cq_poller_proc(arg);
}


struct PfRdmaDevContext* build_context(struct ibv_context* rdma_context)
{
	struct PfRdmaDevContext* rdma_dev_ctx;
	struct ibv_device_attr device_attr;
	int rc;
	pthread_mutex_lock(&global_dev_lock);
	rc = ibv_query_device(rdma_context, &device_attr);
	if (rc)
	{
		S5LOG_ERROR("ibv_query_device failed, rc:%d", rc);
		pthread_mutex_unlock(&global_dev_lock);
		return NULL;
	}

	for (int i=0; i<4; i++)
	{
		if (global_dev_ctx[i] == NULL)
		{
			rdma_dev_ctx = (struct PfRdmaDevContext*)calloc(1, sizeof(struct PfRdmaDevContext));
			memcpy(&rdma_dev_ctx->dev_attr, &device_attr, sizeof(struct ibv_device_attr));
			rdma_dev_ctx->ctx = rdma_context;
			rdma_dev_ctx->pd = ibv_alloc_pd(rdma_context);
			rc = register_rep_context_buf(rdma_dev_ctx->pd, i);
			if (rc)
			{
				S5LOG_ERROR("register_rep_context_buf failed, rc:%d", rc);
				break;
			}
			rc = register_disp_context_buf(rdma_dev_ctx->pd, i);
			if (rc)
			{
				S5LOG_ERROR("register_disp_context_buf failed, rc:%d", rc);
				break;
			}
			rdma_dev_ctx->comp_channel = ibv_create_comp_channel(rdma_context);
			rdma_dev_ctx->cq = ibv_create_cq(rdma_context, 512, NULL, rdma_dev_ctx->comp_channel, 0);
			ibv_req_notify_cq(rdma_dev_ctx->cq, 0);
			rdma_dev_ctx->idx = i;
			rdma_dev_ctx->poller = new PfPoller();
			rdma_dev_ctx->poller->init(format_string("RDMA_poll_%d", i).c_str(), 128);
			rdma_dev_ctx->poller->dev_ctx = rdma_dev_ctx;
			rdma_dev_ctx->poller->add_fd(rdma_dev_ctx->comp_channel->fd, EPOLLIN, on_rdma_cq_event, rdma_dev_ctx);
			global_dev_ctx[i] = rdma_dev_ctx;
		}
		if (global_dev_ctx[i]->ctx == rdma_context)
		{
			pthread_mutex_unlock(&global_dev_lock);
			return global_dev_ctx[i];
		}
	}
	pthread_mutex_unlock(&global_dev_lock);
	return NULL;
}

void PfRdmaConnection::build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
    memset(qp_attr, 0, sizeof(*qp_attr));
    qp_attr->send_cq = dev_ctx->cq;
    qp_attr->recv_cq = dev_ctx->cq;
    qp_attr->qp_type = IBV_QPT_RC;
    qp_attr->cap.max_send_wr = 512;
    qp_attr->cap.max_recv_wr = 512;
    qp_attr->cap.max_send_sge = 1;
    qp_attr->cap.max_recv_sge = 1;
}

int on_route_resolved(struct rdma_cm_id* id)
{
	int rc = 0;
	struct rdma_conn_param cm_params;
	struct ibv_qp_init_attr qp_attr;
	struct PfRdmaConnection* conn = (struct PfRdmaConnection*)id->context;
	conn->dev_ctx = build_context(id->verbs);
	if(conn->dev_ctx == NULL)
	{
		S5LOG_ERROR("build_context");
		return -1;
	}

	conn->build_qp_attr(&qp_attr);
	rc = rdma_create_qp(id, conn->dev_ctx->pd, &qp_attr);
	if (rc)
	{
		S5LOG_ERROR("create_qp failed, errno:%d", errno);
        return rc;
	}
	conn->qp = id->qp;
	PfHandshakeMessage* hmsg = new PfHandshakeMessage;
	hmsg->hsqsize = 128;
	hmsg->vol_id = conn->vol_id;
	hmsg->protocol_ver = PROTOCOL_VER;
	memset(&cm_params, 0, sizeof(cm_params));
	int outstanding_read = conn->dev_ctx->dev_attr.max_qp_rd_atom;
	if (outstanding_read > 128)
		outstanding_read = 128;
	cm_params.private_data = hmsg;
	cm_params.private_data_len = sizeof(PfHandshakeMessage);
	cm_params.responder_resources = (uint8_t)outstanding_read;
	cm_params.initiator_depth = (uint8_t)outstanding_read;
	cm_params.retry_count = 7;
	cm_params.rnr_retry_count = 7;
	rc = rdma_connect(id, &cm_params);
	if(rc)
	{
		S5LOG_ERROR("rdma_connect failed, errno:%d", errno);
		return rc;
	}
	return 0;
}

int on_connection(struct rdma_cm_id *id)
{
	struct PfRdmaConnection *conn = (struct PfRdmaConnection *)id->context;
	conn->state = CONN_OK;
	return 0;
}

int on_disconnect(struct rdma_cm_id *id)
{
	struct PfRdmaConnection *conn = (struct PfRdmaConnection *)id->context;
	conn->do_close();
	return 0;
}

void* process_event_channel(void *arg) {
	struct rdma_event_channel* ec = (struct rdma_event_channel *)arg;
	struct rdma_cm_event *event = NULL;
	while(rdma_get_cm_event(ec, &event) == 0) {
		struct rdma_cm_event event_copy;
		memcpy(&event_copy, event, sizeof(*event));
		rdma_ack_cm_event(event);
		switch(event_copy.event)
		{
			case RDMA_CM_EVENT_ADDR_RESOLVED:
				S5LOG_DEBUG("get event RDMA_CM_EVENT_ADDR_RESOLVED\n");
				on_addr_resolved(event_copy.id);
				break;
			case RDMA_CM_EVENT_ROUTE_RESOLVED:
				S5LOG_DEBUG("get event RDMA_CM_EVENT_ROUTE_RESOLVED\n");
				on_route_resolved(event_copy.id);
				break;
			case RDMA_CM_EVENT_ESTABLISHED:
				S5LOG_DEBUG("get event RDMA_CM_EVENT_ESTABLISHED\n");
				on_connection(event_copy.id);
				break;
			case RDMA_CM_EVENT_DISCONNECTED:
				S5LOG_DEBUG("get event RDMA_CM_EVENT_DISCONNECTED\n");
				on_disconnect(event_copy.id);
				break;
			default:
				break;
		}
	}
	return 0;
}

PfRdmaConnection* PfRdmaConnection::connect_to_server(const std::string ip, int port, PfPoller *poller, uint64_t vol_id, int io_depth, int timeout_sec)
{
	int rc = 0;
	struct addrinfo* addr;
	rc = getaddrinfo(ip.c_str(), NULL, NULL, &addr);
	if(rc)
	{
		S5LOG_ERROR("getaddrinfo failed, rc:%d", rc);
		return NULL;
	}
	((struct sockaddr_in*)addr->ai_addr)->sin_port = htons((uint16_t)port);
	S5LOG_DEBUG("server ip:%s.\n", ip.c_str());
	PfRdmaConnection* conn = new PfRdmaConnection();
	conn->ec = rdma_create_event_channel();
	if(conn->ec == NULL)
	{
		S5LOG_ERROR("rdma_create_event_channel failed, errno:%d", errno);
		goto failure_lay1;
	}
	rc = rdma_create_id(conn->ec, &conn->rdma_id, NULL, RDMA_PS_TCP);
	if(rc)
	{
		S5LOG_ERROR("rdma_create_id failed, errno:%d", errno);
		goto failure_lay1;
	}
	conn->vol_id = vol_id;
	conn->rdma_id->context = conn;
	rc = rdma_resolve_addr(conn->rdma_id, NULL, addr->ai_addr, 500);
	if(rc)
	{
		S5LOG_ERROR("rdma_resolve_addr failed, errno:%d", errno);
		goto failure_lay1;
	}
	rc = pthread_create(&conn->tid, NULL, process_event_channel, conn->ec);
	if(rc)
	{
		S5LOG_ERROR("pthread_create failed, rc:%d", rc);
		goto failure_lay1;
	}
	for (int i=0; i<10 && conn->state != CONN_OK; i++){
		sleep(1);
	}
	freeaddrinfo(addr);
	return conn;
failure_lay1:
    delete conn;
    return NULL;
}

PfRdmaConnection::PfRdmaConnection(void)
{
    this->event = NULL;
    this->rdma_id = NULL;
    this->ec = NULL;
}

PfRdmaConnection::~PfRdmaConnection(void)
{
}

int PfRdmaConnection::post_recv(BufferDescriptor* buf)
{
    int rc = 0;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    buf->conn = this;
    buf->wr_op = RDMA_WR_RECV;

    wr.wr_id = (uint64_t)buf;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uint64_t)buf->buf;
    sge.length = buf->data_len;
    sge.lkey = buf->mrs[this->dev_ctx->idx]->lkey;

    rc = ibv_post_recv(rdma_id->qp, &wr, &bad_wr);
    if (rc != 0)
    {
        S5LOG_ERROR("rdma post_recv failed, errno:%d", errno);
        return rc;
    }
    return 0;
}

int PfRdmaConnection::post_send(BufferDescriptor* buf)
{
    int rc = 0;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    buf->conn = this;
    buf->wr_op = RDMA_WR_SEND; 

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uint64_t)buf;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;

    sge.addr = (uint64_t)buf->buf;
    sge.length = buf->data_len;
    sge.lkey = buf->mrs[this->dev_ctx->idx]->lkey;

    rc = ibv_post_send(rdma_id->qp, &wr, &bad_wr);
    if (rc)
    {
        S5LOG_ERROR("ibv_post_send failed, rc:%d", rc);
        return rc;
    }
    return 0;
}

int PfRdmaConnection::post_read(BufferDescriptor* buf, uintptr_t raddr, uint32_t rkey)
{
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    buf->conn = this;
    buf->wr_op = RDMA_WR_RECV;

    wr.wr_id = (uint64_t)buf;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uint64_t)raddr;
    wr.wr.rdma.rkey = rkey;

    sge.addr = (uint64_t)buf->buf;
    sge.length = buf->data_len;
    sge.lkey = buf->mrs[this->dev_ctx->idx]->lkey;

    int rc = ibv_post_send(rdma_id->qp, &wr, &bad_wr);
    if (rc)
    {
        S5LOG_ERROR("ibv_post_send failed, rc:%d", rc);
        return rc;
    }
    return 0;
}

int PfRdmaConnection::post_write(BufferDescriptor* buf, uintptr_t raddr, uint32_t rkey)
{
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    buf->conn = this;
    buf->wr_op = RDMA_WR_WRITE;

    wr.wr_id = (uint64_t)buf;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uint64_t)raddr;
    wr.wr.rdma.rkey = rkey;

    sge.addr = (uint64_t)buf->buf;
    sge.length = buf->data_len;
    sge.lkey = buf->mrs[this->dev_ctx->idx]->lkey;

    int rc = ibv_post_send(rdma_id->qp, &wr, &bad_wr);
    if (rc)
    {
        S5LOG_ERROR("ibv_post_send failed, rc:%d", rc);
        return rc;
    }
    return 0;
}

int PfRdmaConnection::post_write(BufferDescriptor* buf)
{
}

int PfRdmaConnection::post_read(BufferDescriptor* buf)
{
}

int PfRdmaConnection::do_close()
{
	int rc = 0;
	struct rdma_cm_id* id = this->rdma_id;
	this->dev_ctx->poller->del_fd(this->dev_ctx->comp_channel->fd);
	rdma_destroy_qp(id);
	for(int i=0; i<MAX_DISPATCHER_COUNT; i++){
		rc = 0;
		if (disp_mem_pool[i] == 0)
			break;
		struct disp_mem_pool* mem_pool = disp_mem_pool[i];
		rc = ibv_dereg_mr(mem_pool->cmd_pool.mrs[this->dev_ctx->idx]);
		if(rc)
		{
		    S5LOG_ERROR("ibv_dereg_mr disp cmd_pool failed, rc:%d", rc);
		}
		rc = ibv_dereg_mr(mem_pool->data_pool.mrs[this->dev_ctx->idx]);
		if(rc)
		{
		    S5LOG_ERROR("ibv_dereg_mr disp data_pool failed, rc:%d", rc);
		}
		rc = ibv_dereg_mr(mem_pool->reply_pool.mrs[this->dev_ctx->idx]);
		if(rc)
		{
		    S5LOG_ERROR("ibv_dereg_mr disp reply_pool failed, rc:%d", rc);
		}
	}
	for(int i=0; i<MAX_REPLICATOR_COUNT; i++){
		rc = 0;
		if(rep_mem_pool[i] == 0)
			break;
		struct replicator_mem_pool *mem_pool = rep_mem_pool[i];
		rc = ibv_dereg_mr(mem_pool->cmd_pool.mrs[this->dev_ctx->idx]);
		if(rc)
		{
		    S5LOG_ERROR("ibv_dereg_mr rep reply_pool failed, rc:%d", rc);
		}
		rc = ibv_dereg_mr(mem_pool->reply_pool.mrs[this->dev_ctx->idx]);
		if(rc)
		{
		    S5LOG_ERROR("ibv_dereg_mr rep reply_pool failed, rc:%d", rc);
		}
	}
	rc = rdma_destroy_id(id);
	if(rc) {
		S5LOG_ERROR("rdma_destroy_id  failed, rc:%d", rc);
	}
	return rc;
}
