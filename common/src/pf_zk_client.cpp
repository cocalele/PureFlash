//
// Created by lele on 10/26/19.
//

#include <unistd.h>
#include <semaphore.h>

#include "pf_zk_client.h"
#include "zookeeper.h"
#include "pf_log.h"
#include "pf_utils.h"

using namespace std;
static  void on_zk_event(zhandle_t *zh, int type, int state, const char *path,void *watcherCtx)
{
	S5LOG_INFO("ZK event:%d state:%d path:%s", type, state, path);
}

int PfZkClient::init(const char *zk_ip, int zk_timeout, const char* cluster_name) {
	int rc = 0;
	this->cluster_name = cluster_name;
	zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
	S5LOG_INFO("Connecting to zk:%s ...", zk_ip);
	zkhandle = zookeeper_init(zk_ip, on_zk_event, zk_timeout, NULL, this, 0);
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

PfZkClient::~PfZkClient() {
	if(zkhandle)
		zookeeper_close(zkhandle);
}

struct ZkSem
{
public:
	sem_t sem;
	void* data;
	int zk_rc;
	ZkSem(void* data): zk_rc(0)
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

int PfZkClient::create_node(const std::string& node_path, bool is_ephemeral, const char* node_data)
{


	string full_path=node_path;
	if(node_path[0] != '/')
		full_path="/pureflash/"+cluster_name+"/"+node_path;
	if(zoo_exists(zkhandle, full_path.c_str(), 0, NULL) == ZOK){
		if(is_ephemeral){
			S5LOG_WARN("Emphereal zk node:%s already exists! arg node_path:%s", full_path.c_str(), node_path.c_str());
			return ZNODEEXISTS;
		}
		return ZOK;
	}
	size_t pos = full_path.find_last_of('/');
	string parent = full_path.substr(0, pos);
	int rc = ZOK;
	if(!parent.empty())
	{
		rc = zoo_exists(zkhandle, parent.c_str(), 0, NULL);

		if(rc != ZOK) {
			if (rc == ZNONODE) {
				rc = create_node(parent, 0, NULL);
				if(rc != ZOK) {
					return rc;
				}
			} else {
				S5LOG_ERROR("Failed to check existence ZK node:%s, node_path:%s, rc:%d", full_path.c_str(), node_path.c_str(), rc);
				return rc;
			}
		}
	}
	rc = zoo_create(zkhandle, full_path.c_str(), node_data, node_data ? (int)strlen(node_data) : 0, &ZOO_OPEN_ACL_UNSAFE,
		is_ephemeral ? ZOO_EPHEMERAL : 0, NULL, 0);
	if(rc != ZOK) {
		S5LOG_ERROR("Failed to create ZK node:%s, node_path:%s, rc:%d", full_path.c_str(), node_path.c_str(), rc);
	}
	return rc;
}

static void zk_watch_cbk(zhandle_t* zh, int type,
	int state, const char* path, void* watcherCtx)
{
	struct ZkSem* ctx = (struct ZkSem*)watcherCtx;
	sem_post(&ctx->sem);
}
static int cmpstringp(const void* p1, const void* p2)
{
	/* The actual arguments to this function are "pointers to
	   pointers to char", but strcmp(3) arguments are "pointers
	   to char", hence the following cast plus dereference. */

	return strcmp(*(const char**)p1, *(const char**)p2);
}

int PfZkClient::wait_lock(const std::string& lock_path, const char* myid)
{
	string full_path = lock_path;
	full_path = "/pureflash/" + cluster_name + "/" + lock_path;
	int rc = create_node(full_path, false, NULL);
	if(rc){
		S5LOG_ERROR("Failed to create lock node");
		return rc;
	}
	full_path = "/pureflash/" + cluster_name + "/" + lock_path + "/lock";
	

	char lock_ephemeral_node[256];
	rc = zoo_create(zkhandle, full_path.c_str(), myid, myid ? (int)strlen(myid) : 0, &ZOO_OPEN_ACL_UNSAFE,
		ZOO_EPHEMERAL_SEQUENTIAL, lock_ephemeral_node, sizeof(lock_ephemeral_node));
	if (rc != ZOK) {
		S5LOG_ERROR("Failed to create ZK node:%s, node_path:%s, rc:%d", full_path.c_str(), lock_path.c_str(), rc);
		return rc;
	}
	S5LOG_INFO("Waiting lock with node:%s, myid:%s", lock_ephemeral_node, myid);
	full_path = "/pureflash/" + cluster_name + "/" + lock_path;
	struct ZkSem ctx(NULL);

	do {
		struct String_vector children={0};
		rc = zoo_wget_children(zkhandle, full_path.c_str(), zk_watch_cbk, &ctx, &children);
		if (rc != ZOK) {
			S5LOG_ERROR("Failed to create ZK lock:%s, lock_path:%s, rc:%d", full_path.c_str(), lock_path.c_str(), rc);
			return rc;
		}
		DeferCall _a([&children]() { deallocate_String_vector(&children); });
		if(children.count <= 0){
			S5LOG_ERROR("Lock watch get empty children, network connection wrong");
			return -ECONNABORTED;
			
		}
		qsort(children.data, children.count, sizeof(char*), cmpstringp);
		if(strcmp(children.data[0], lock_ephemeral_node) == 0) {
			S5LOG_INFO("Get lock %s!", full_path.c_str());
			zoo_remove_watches(zkhandle, full_path.c_str(), ZooWatcherType::ZWATCHTYPE_CHILD, zk_watch_cbk, &ctx, 1);
			return 0;
		}
		rc = ctx.wait();
	}while(rc == 0);

	return rc;
}

int PfZkClient::delete_node(const std::string& node_path)
{
	return zoo_delete(zkhandle, node_path.c_str(), -1);
}
