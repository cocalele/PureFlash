//
// Created by lele on 10/26/19.
//

#include <unistd.h>
#include <semaphore.h>

#include "s5_zk_client.h"
#include "zookeeper.h"
#include "s5_log.h"

using namespace std;

int S5ZkClient::init(const char *zk_ip, int zk_timeout, const char* cluster_name) {
	int rc = 0;
	this->cluster_name = cluster_name;
	zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
	S5LOG_INFO("Connecting to zk:%s ...", zk_ip);
	zkhandle = zookeeper_init(zk_ip, NULL, zk_timeout, NULL, NULL, 0);
	if (zkhandle == NULL) {
		rc = -errno;
		S5LOG_ERROR("Failed to connect zk:%s, rc:%d ", zk_ip, rc);
		return rc;
	}
	int state;
	for (int i = 0; i < 100; i++) {
		usleep(200000);
		state = zoo_state(zkhandle);
		if (state == ZOO_CONNECTED_STATE)
			break;
	}
	if (state != ZOO_CONNECTED_STATE) {
		S5LOG_ERROR("Failed to wait zk state to OK, state:%d ", state);
		return -ETIMEDOUT;
	}
	return 0;
}

S5ZkClient::~S5ZkClient() {
	if(zkhandle)
		zookeeper_close(zkhandle);
}

struct ZkSem
{
public:
	sem_t sem;
	void* data;
	int zk_rc;
	ZkSem(void* data)
	{
		this->data = data;
		sem_init(&sem, 0, 0);
	}
	int wait()
	{
		int rc = sem_wait(&sem);
		if(rc != 0)
			return -errno;
		return zk_rc;
	}
};

void zk_completion(int rc, const struct Stat *stat, const void *data)
{
	ZkSem* s = (ZkSem*)data;
	s->zk_rc = rc;
	sem_post(&s->sem);
}

int S5ZkClient::create_node(const std::string& node_path, bool is_ephemeral, const char* node_data)
{


	string full_path=node_path;
	if(node_path[0] != '/')
		full_path="/"+cluster_name+"/"+node_path;
	size_t pos = full_path.find_last_of('/');
	string parent = full_path.substr(0, pos);
	int rc = zoo_exists(zkhandle, parent.c_str(), 0, NULL);
	if(rc == ZNONODE)
	{
		rc = create_node(parent, 0, NULL);
		if(rc)
		{
			return rc;
		}
	}
	else if(rc == ZOK)
	{
		rc = zoo_create(zkhandle, full_path.c_str(), node_data, (int)strlen(node_data), &ZOO_CREATOR_ALL_ACL,
			is_ephemeral ? ZOO_EPHEMERAL : 0, NULL, 0);
		if(rc != ZOK)
		{
			S5LOG_ERROR("Failed to create ZK node:%s, rc:%d", full_path.c_str(), rc);
		}
		return rc;
	}
	else
	{
		S5LOG_ERROR("Failed to create ZK node:%s, rc:%d", parent.c_str(), rc);
	}
	return rc;
}
