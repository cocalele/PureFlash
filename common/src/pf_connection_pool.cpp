#include <mutex>
#include "pf_connection_pool.h"
#include "pf_connection.h"
#include "pf_tcp_connection.h"

using namespace  std;

int PfConnectionPool::init(int size, PfPoller* poller, void* owner, uint64_t  vol_id, int io_depth, work_complete_handler _handler)
{
	pool_size = size;
	this->poller = poller;
	this->owner = owner;
	this->io_depth = io_depth;
	on_work_complete = _handler;
	this->vol_id = vol_id;
	return 0;
}

void client_on_tcp_close(PfConnection* c)
{
	//c->dec_ref(); //will not dec_ref for client connection. only dec_ref when connection removed from pool

}

PfConnection* PfConnectionPool::get_conn(const std::string& ip)
{
	std::lock_guard<std::mutex> _l(mtx);
	auto pos = ip_id_map.find(ip);
	if (pos != ip_id_map.end()) {
		auto c = pos->second;
		if(c->state == CONN_OK)
			return c;
		else {
			S5LOG_WARN("Connection:%s in CLOSED state, will reconnect.", c->connection_info.c_str());
			ip_id_map.erase(pos);
			c->dec_ref();
		}
	}

	PfTcpConnection *c = PfTcpConnection::connect_to_server(ip, 49162, poller, vol_id, io_depth, 4/*connection timeout*/);
	c->add_ref(); //this ref hold by pool, decreased when remove from connection pool
	c->on_work_complete = on_work_complete;
	c->on_close = client_on_tcp_close;
	c->master = this->owner;
	ip_id_map[ip] = c;
	return c;
}

void PfConnectionPool::close_all()
{
	S5LOG_INFO("Close all connection in pool, %d connections to release", ip_id_map.size());

	for(auto it = ip_id_map.begin(); it != ip_id_map.end(); ++it) {
		auto c = it->second;
		c->close();
		c->dec_ref();
	}
	ip_id_map.clear();
}
