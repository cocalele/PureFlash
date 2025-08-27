#ifndef pf_connection_pool_h__
#define pf_connection_pool_h__
#include <string>
#include <mutex>
#include <map>
#include "pf_connection.h"

class PfConnection;
class PfPoller;

typedef void (*conn_close_handler)(PfConnection*);
class PfConnectionPool
{
public:
	enum connection_type conn_type;
	PfConnectionPool() : pool_size(0){ }
	int init(int size, PfPoller* poller, void* owner, uint64_t vol_id, int io_depth, enum connection_type type, work_complete_handler _handler, conn_close_handler close_handler);
	PfConnection* get_conn(const std::string& ip, enum connection_type) noexcept ;
	void close_all();
public:
	std::map<std::string, PfConnection*> ip_id_map;
	std::mutex mtx;
	int pool_size;
	int io_depth;
	PfPoller* poller;
	union{
		PfClientVolume* volume; //used in client side
		PfReplicator* replicator;
		void* owner;
	};
	uint64_t vol_id;
	work_complete_handler on_work_complete;
	conn_close_handler  on_conn_closed;
};

#endif // pf_connection_pool_h__
