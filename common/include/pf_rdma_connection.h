#ifndef pf_rdma_connection_h__
#define pf_rdma_connection_h__
#include "pf_message.h"
#include "pf_connection.h"
#include "pf_event_queue.h"
#include "pf_buffer.h"
#include "pf_poller.h"
#include "pf_utils.h"
#include <rdma/rdma_cma.h>

class PfPoller;
class PfClientVolume;
class BufferDescriptor;

struct PfRdmaDevContext {
    struct ibv_context* ctx;
    struct ibv_pd* pd;
    struct ibv_cq* cq;
    struct ibv_comp_channel* comp_channel;
    struct ibv_device_attr dev_attr;
    PfPoller *poller;
    int idx;
};

class PfRdmaConnection : public PfConnection
{
public:
	PfRdmaConnection(void);
	virtual ~PfRdmaConnection(void);

	struct ibv_mr* recv_mr;
	struct ibv_mr* send_mr;
	struct ibv_pd* pd;
	struct ibv_cq* cq;
	struct ibv_qp* qp;
	struct ibv_comp_channel* comp_channel;
	struct rdma_event_channel *ec;
	struct rdma_cm_id* rdma_id;
	struct rdma_cm_event* event;
	struct PfRdmaDevContext* dev_ctx;

	int socket_fd;
	std::string ip;
	pthread_t tid;
	uint64_t vol_id;

	virtual int post_recv(BufferDescriptor* buf);
	virtual int post_send(BufferDescriptor* buf);
	virtual int post_read(BufferDescriptor* buf, uintptr_t raddr, uint32_t rkey);
	virtual int post_read(BufferDescriptor* buf);
	virtual int post_write(BufferDescriptor* buf);
	virtual int do_close(void);

	void start_send(BufferDescriptor* bd);
	void start_send(BufferDescriptor* bd, void* buf);
	void start_recv(BufferDescriptor* bd);
	void start_recv(BufferDescriptor* bd, void* buf);

	static void on_send_q_event(int fd, uint32_t event, void* c);
	static void on_recv_q_event(int fd, uint32_t event, void* c);
	static void on_socket_event(int fd, uint32_t event, void* c);
	static PfRdmaConnection* connect_to_server(const std::string ip, int port, PfPoller *poller, uint64_t vol_id, int io_depth, int timeout_sec);

	int init(int sock_fd, PfPoller *poller, int send_q_depth, int recv_q_depth);
	void init_rdma_client_connection(const std::string ip, int port, PfPoller *poller, uint64_t vol_id, int io_depth, int timeout_sec);
    void build_qp_attr(struct ibv_qp_init_attr *qp_attr);

	void* recv_buf;
	int recved_len;              ///< how many has received.
	int wanted_recv_len;		///< want received length.
	BufferDescriptor* recv_bd;

	void* send_buf;
	int sent_len;				///< how many has sent
	int wanted_send_len;		///< want to send length of data.
	BufferDescriptor*  send_bd;

	BOOL readable;              ///< socket buffer have data to read yes or no.
	BOOL writeable;             ///< socket buffer can send data yes or no.

//	PfFixedSizeQueue<PfServerIocb*> conn_cmd_wrs;
//	PfFixedSizeQueue<PfServerIocb*> free_cmd_wrs;

	BOOL need_reconnect;

	PfEventQueue recv_q;
	PfEventQueue send_q;

	bool is_client; //is this a client side connection

private:
	int do_receive();
	int do_send();
	int rcv_with_error_handle();
	int send_with_error_handle();
	void flush_wr();
};

#ifdef __cplusplus
extern "C" {
#endif
struct PfRdmaDevContext* build_context(struct ibv_context* rdma_context);
#ifdef __cplusplus
}
#endif

#endif // pf_tcp_connection_h__
