#ifndef pf_connection_h__
#define pf_connection_h__
#include "pf_buffer.h"

#define CONN_INIT 0
#define CONN_OK 1
#define CONN_CLOSED 2
#define CONN_CLOSING 3

#define TRANSPORT_TCP 1
#define TRANSPORT_RDMA 2

#define CONN_ROLE_SERVER 1
#define CONN_ROLE_CLIENT 2

#define PROTOCOL_VER 1
class S5ClientVolumeInfo;

typedef int(*work_complete_handler)(BufferDescriptor* bd, WcStatus complete_status, S5Connection* conn, void* cbk_data);
class S5Connection
{
public:
	int ref_count = 0;
	work_complete_handler on_work_complete;
	int state;
	int transport;
	int role;
	uint64_t last_heartbeat_time;
	int io_depth;
	std::string connection_info;
	void* ulp_data; //up layer data
	std::string peer_ip;
	int peer_port;
	int inflying_heartbeat;

	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;

	S5ClientVolumeInfo* volume;

	S5Connection();
	virtual ~S5Connection();
	virtual int post_recv(BufferDescriptor* buf)=0;
	virtual int post_send(BufferDescriptor* buf)=0;
	virtual int post_read(BufferDescriptor* buf)=0;
	virtual int post_write(BufferDescriptor* buf)=0;
	virtual int do_close() = 0;
	int close();
	int send_heartbeat();

	void (*on_close)(S5Connection*);
	void (*on_destroy)(S5Connection*);

	inline void add_ref() {__sync_fetch_and_add(&ref_count, 1);}
	inline void dec_ref() {
		if (__sync_sub_and_fetch(&ref_count, 1) == 0)
		{
			if (state == CONN_OK)
			{
				close();
			}
			on_destroy(this);
			delete this;
		}

	}

	int init_mempools();
};

int parse_net_address(const char* ipv4, unsigned short port, /*out*/struct sockaddr_in* ipaddr);

#endif // pf_connection_h__
