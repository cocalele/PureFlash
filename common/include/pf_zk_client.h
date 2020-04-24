//
// Created by lele on 10/26/19.
//

#ifndef PUREFLASH_S5_ZK_CLIENT_H
#define PUREFLASH_S5_ZK_CLIENT_H
#include <string>
#include "string.h"

#include "zookeeper.h"

class S5ZkClient {
public:
	S5ZkClient(){ zkhandle = NULL; }
	~S5ZkClient();
	int init(const char* zk_ip, int zk_timeout, const char* cluster_name);
	int create_node(const std::string& node_path, bool is_ephemeral, const char* node_data);

	//members:
	zhandle_t *zkhandle;
	std::string cluster_name;
};


#endif //PUREFLASH_S5_ZK_CLIENT_H
