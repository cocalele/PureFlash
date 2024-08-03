#ifndef pf_rdma_connection_h__
#define pf_rdma_connection_h__
#include "pf_message.h"
#include "pf_connection.h"
#include "pf_event_queue.h"
#include "pf_buffer.h"
#include "pf_poller.h"
#include "pf_utils.h"
#include <rdma/rdma_cma.h>
#ifdef WITH_SPDK_BDEV
#include "spdk/env.h"
#include "spdk/thread.h"
#endif

class PfPoller;
class PfClientVolume;
class BufferDescriptor;

#define MAX_RDMA_DEVICE (4)
#define MAX_POLLER_COUNT (16)

#define CQ_POLLER_COUNT (16)
#define CQ_POLLER_CLIENT_COUNT (8)

struct PfRdmaPoller {
	pthread_t tid;
	char name[32];
	struct PfRdmaDevContext *prp_dev_ctx;
	int prp_idx;
	struct ibv_cq* prp_cq;
	struct ibv_comp_channel* prp_comp_channel;
	PfPoller poller;
	PfRdmaPoller():tid(0){}
	~PfRdmaPoller() {
		if (tid > 0) {
			pthread_cancel(tid);
			pthread_join(tid, NULL);
			tid = 0;
		}
	}
};

struct PfRdmaDevContext {
    struct ibv_context* ctx;
    struct ibv_pd* pd;
    struct ibv_device_attr dev_attr;
#ifdef WITH_SPDK_BDEV
    struct ibv_cq* prp_cq;
    struct ibv_comp_channel* prp_comp_channel;
    struct spdk_poller *client_completion_poller;
#endif
    int idx;
	int cq_poller_cnt;
	int client_cq_poller_cnt;
	int next_server_cq_poller_idx;
	int next_client_cq_poller_idx;
	struct PfRdmaPoller prdc_poller_ctx[MAX_POLLER_COUNT];
};

class PfRdmaConnection : public PfConnection
{
public:
	PfRdmaConnection(void);
	virtual ~PfRdmaConnection(void);

	struct ibv_mr* recv_mr = NULL;
	struct ibv_mr* send_mr = NULL;
	struct ibv_pd* pd = NULL;
	struct ibv_cq* cq = NULL;
	struct ibv_qp* qp = NULL;
	struct ibv_comp_channel* comp_channel = NULL;
	struct rdma_event_channel *ec = NULL;
	struct rdma_cm_id* rdma_id = NULL;
	struct rdma_cm_event* event;
	struct PfRdmaDevContext* dev_ctx = NULL;
	int prc_cq_poller_idx;

	std::string ip;
	pthread_t tid = 0;
	uint64_t vol_id = 0;

	virtual int post_recv(BufferDescriptor* buf);
	virtual int post_send(BufferDescriptor* buf);
	virtual int post_read(BufferDescriptor* buf, uintptr_t raddr, uint32_t rkey);
	virtual int post_read(BufferDescriptor* buf);
	virtual int post_write(BufferDescriptor* buf, uintptr_t raddr, uint32_t rkey);
	virtual int post_write(BufferDescriptor* buf);
	virtual int do_close(void);


	static PfRdmaConnection* connect_to_server(const std::string ip, int port, PfPoller *poller, uint64_t vol_id, int io_depth, int timeout_sec);

	int init(int sock_fd, PfPoller *poller, int send_q_depth, int recv_q_depth);
	void init_rdma_client_connection(const std::string ip, int port, PfPoller *poller, uint64_t vol_id, int io_depth, int timeout_sec);
    void build_qp_attr(struct ibv_qp_init_attr *qp_attr);




//	PfFixedSizeQueue<PfServerIocb*> conn_cmd_wrs;
//	PfFixedSizeQueue<PfServerIocb*> free_cmd_wrs;

	BOOL need_reconnect;


	bool is_client; //is this a client side connection

	std::vector<PfServerIocb*> used_iocb;
};

#ifdef __cplusplus
extern "C" {
#endif
struct PfRdmaDevContext* build_context(struct ibv_context* rdma_context);
#ifdef __cplusplus
}
#endif

#endif // pf_tcp_connection_h__
