#include <lock_guard>
#include "s5_connection_pool.h"
#include "s5_connection.h"
#include "s5_tcp_connection.h"

using namespace  std;


int S5ConnectionPool::init(int size, S5Poller* poller, int io_depth, work_complete_handler _handler)
{
	pool_size = size;
	this->poller = poller;
	this->io_depth = io_depth;
	on_work_complete = _handler;
}

int S5ConnectionPool::get_conn(const std::string& ip)
{
	std::lock_guard _l(mtx);
	auto pos = ip_id_map.find(ip);
	if (pos != ip_id_map.end())
		return pos->second;
	S5TcpConnection *c = S5TcpConnection::connect_to_server(ip, 49181, poller, volume->io_depth);
	c->on_work_complete = on_work_complete;
	ip_id_map[ip] = c;
	c->add_ref();
	return c;
}




/**
* @return 0 on success. On error, a negative error code is returned.
* @retcode -ENOMEM, no memory to init hash table
*/
int qfa_init_conn_pool(struct qfa_connection_pool* pool, completion_handler comp_handler,
	struct qfa_client_volume_priv* volume, int max_io_depth, enum transport_type transport)
{
	int rc;
	rc = ht_init(&pool->conn_table, HT_VALUE_CONST, 0.5, 127); //we will not have too much node
	if (rc != 0)
		return rc;
	pool->comp_handler = comp_handler;
	pool->volume = volume;
	pool->max_io_depth = max_io_depth;
	pool->transport = transport;
	return 0;
}

void qfa_close_all_conn(struct qfa_connection_pool* pool)
{
	qfa_log(NEON_LOG_INFO, "Close all connection in pool, %d connections to release", ht_size(&pool->conn_table));
	struct hash_table_value_iterator* it = ht_create_value_iterator(&pool->conn_table);
	hash_entry* entry = ht_next(it);
	while (entry != NULL)
	{
		struct qfa_connection* v = entry->value;
		entry = ht_next(it);
		if (v)
		{
			qfa_close_conn(v);
			conn_dec_ref(v);
		}
	}
	ht_destroy_value_iterator(it);
	ht_clear(&pool->conn_table);
	pool->volume->connected = 0;
}

void qfa_release_conn_pool(struct qfa_connection_pool* pool)
{
	qfa_close_all_conn(pool);
	ht_destroy(&pool->conn_table);
}
