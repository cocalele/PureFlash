#ifndef s5_connection_h__
#define s5_connection_h__
#include "s5_buffer.h"

#define CONN_INIT 0
#define CONN_OK 1
#define CONN_CLOSED 2

#define TRANSPORT_TCP 1
#define TRANSPORT_RDMA 2

#define CONN_ROLE_SERVER 1
#define CONN_ROLE_CLIENT 2

class S5Connection
{
public:
	S5Connection();
	virtual ~S5Connection();
	int ref_count = 0;
	int state;
	int transport;
	int role;
	uint64_t last_heartbeat_time;
	int io_depth;
	std::string connection_info;

	virtual int post_recv(BufferDescriptor* buf)=0;
	virtual int post_send(BufferDescriptor* buf)=0;
	virtual int post_read(BufferDescriptor* buf)=0;
	virtual int post_write(BufferDescriptor* buf)=0;
	virtual int do_close() = 0;
	int close();

	void (*on_close)(S5Connection*);
	void (*on_destroy)(S5Connection*);

	inline void add_ref() {__sync_fetch_and_add(&ref_count, 1);}
	inline void dec_ref(){
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
	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
};

#endif // s5_connection_h__
