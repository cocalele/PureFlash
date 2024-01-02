#include <mutex>
#include "pf_connection_pool.h"
#include "pf_connection.h"
#include "pf_tcp_connection.h"
#include "pf_rdma_connection.h"

using namespace  std;

int PfConnectionPool::init(int size, PfPoller* poller, void* owner, uint64_t  vol_id, int io_depth, enum connection_type type,
						   work_complete_handler _handler, conn_close_handler close_handler)
{
	pool_size = size;
	this->poller = poller;
	this->owner = owner;
	this->io_depth = io_depth;
	on_work_complete = _handler;
	on_conn_closed = close_handler;
	this->vol_id = vol_id;
	this->conn_type = type;
	return 0;
}


PfConnection* PfConnectionPool::get_conn(const std::string& ip, enum connection_type conn_type) noexcept
{
	std::lock_guard<std::mutex> _l(mtx);
	auto pos = ip_id_map.find(ip);
	if (pos != ip_id_map.end()) {
		auto c = pos->second;
		if(c->state == CONN_OK)
			return c;
		else {
			S5LOG_WARN("Connection:%s in state:%s, will reconnect.", c->connection_info.c_str(), ConnState2Str(c->state));
			ip_id_map.erase(pos);
			c->dec_ref();
		}
	}

	try {
		S5LOG_INFO("Connect to server %s://%s, io_depth:%d ...", conn_type == TCP_TYPE?"TCP":"RDMA", ip.c_str(), io_depth);

		if (conn_type == TCP_TYPE) {
			PfTcpConnection *c = PfTcpConnection::connect_to_server(ip, 49162, poller, vol_id, io_depth, 4/*connection timeout*/);
			c->add_ref(); //this ref hold by pool, decreased when remove from connection pool
			c->on_work_complete = on_work_complete;
			c->on_close = on_conn_closed;
			c->master = this->owner;
			c->conn_type = TCP_TYPE;
			c->io_depth = io_depth;
			ip_id_map[ip] = c;	
			return c;
		}
		else if (conn_type == RDMA_TYPE) {
#ifdef WITH_RDMA
			PfRdmaConnection *c = PfRdmaConnection::connect_to_server(ip, 49160, poller, vol_id, io_depth, 4/*connection timeout*/);
			c->add_ref(); //this ref hold by pool, decreased when remove from connection pool
			c->on_work_complete = on_work_complete;
			c->on_close = on_conn_closed;
			c->master = this->owner;
			c->conn_type = RDMA_TYPE;
			c->io_depth = io_depth;
			ip_id_map[ip] = c;
			return c;
#else
			S5LOG_FATAL("RDMA not enabled, please compile with -DWITH_RDMA=1");
#endif
		}
	}
	catch(std::exception& e) {
		S5LOG_ERROR("Error connect to:%s, exception:%s", ip.c_str(), e.what());
	}
	return NULL;
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
