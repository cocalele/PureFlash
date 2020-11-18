#ifndef pf_connection_h__
#define pf_connection_h__
#include "pf_buffer.h"

#define CONN_INIT 0
#define CONN_OK 1
#define CONN_CLOSED 2
#define CONN_CLOSING 3

const char* ConnState2Str(int conn_state);


#define TRANSPORT_TCP 1
#define TRANSPORT_RDMA 2

#define PROTOCOL_VER 1
class PfClientVolume;
class PfDispatcher;
class PfVolume;
class PfReplicator;

typedef int(*work_complete_handler)(BufferDescriptor* bd, WcStatus complete_status, PfConnection* conn, void* cbk_data);
class PfConnection
{
public:
	int ref_count = 0;
	work_complete_handler on_work_complete;

	union {
		PfClientVolume* volume; //used in client side
		PfVolume* srv_vol; //used in server side
		PfReplicator* replicator;
		void* master;
	};
    PfDispatcher* dispatcher;
	int state;
	int transport;
	uint64_t last_heartbeat_time;
	int io_depth;
	std::string connection_info;
	std::string peer_ip;
	int peer_port;
	int inflying_heartbeat;
	bool unclean_closed = false;

	PfConnection();
	virtual ~PfConnection();
	virtual int post_recv(BufferDescriptor* buf)=0;
	virtual int post_send(BufferDescriptor* buf)=0;
	virtual int post_read(BufferDescriptor* buf)=0;
	virtual int post_write(BufferDescriptor* buf)=0;
	virtual int do_close() = 0;
	int close();
	int send_heartbeat();

	void (*on_close)(PfConnection*);
	void (*on_destroy)(PfConnection*);

	inline void add_ref() {__sync_fetch_and_add(&ref_count, 1);
	//S5LOG_INFO("add_ref conn:0x%x ref_cnt:%d", this, ref_count);
	}
	inline void dec_ref() {
		if (__sync_sub_and_fetch(&ref_count, 1) == 0)
		{
			//S5LOG_INFO("dec_ref conn:0x%x ref_cnt:%d", this, ref_count);
			if (state == CONN_OK)
			{
				close();
			}
			if(on_destroy)
				on_destroy(this);
			delete this;
		}
	}
};

int parse_net_address(const char* ipv4, unsigned short port, /*out*/struct sockaddr_in* ipaddr);
#endif // pf_connection_h__
