#ifndef pf_connection_pool_h__
#define pf_connection_pool_h__
#include <string>
#include <mutex>
#include <map>
#include "pf_connection.h"

class PfConnection;
class PfPoller;
class PfConnectionPool
{
public:
	PfConnectionPool() : pool_size(0){ }
	int init(int size, PfPoller* poller, PfClientVolumeInfo* vol, int io_depth, work_complete_handler _handler);
	PfConnection* get_conn(const std::string& ip);
	void close_all();
public:
	std::map<std::string, PfConnection*> ip_id_map;
	std::mutex mtx;
	int pool_size;
	PfPoller* poller;
	int io_depth;
	PfClientVolumeInfo* volume;
	work_complete_handler on_work_complete;
};

#endif // pf_connection_pool_h__
