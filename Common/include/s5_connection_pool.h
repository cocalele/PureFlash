#ifndef s5_connection_pool_h__
#define s5_connection_pool_h__
#include <string>
#include <mutex>
#include <map>
#include "s5_connection.h"

class S5Connection;
class S5Poller;
class S5ConnectionPool
{
public:
	S5ConnectionPool() : pool_size(0){ }
	int init(int size, S5Poller* poller, S5ClientVolumeInfo* vol, int io_depth, work_complete_handler _handler);
	S5Connection* get_conn(const std::string& ip);
	void close_all();
public:
	std::map<std::string, S5Connection*> ip_id_map;
	std::mutex mtx;
	int pool_size;
	S5Poller* poller;
	int io_depth;
	S5ClientVolumeInfo* volume;
	work_complete_handler on_work_complete;
};

#endif // s5_connection_pool_h__
