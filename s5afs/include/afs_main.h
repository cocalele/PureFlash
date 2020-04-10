/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * This file defines the data structure: toedaemon, and defines its initialization and release func.
 */

#ifndef AFS_MAIN_H
#define AFS_MAIN_H

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <set>
#include <vector>
#include <map>

#include "s5_zk_client.h"
#include "s5_app_ctx.h"
#include "afs_flash_store.h"
#include "s5_replicator.h"
#include "s5_dispatcher.h"


class S5TcpServer;

#define MAX_TRAY_COUNT 32
#define MAX_PORT_COUNT 4

#define DATA_PORT 0
#define REP_PORT 1
class S5Volume;
class S5AfsAppContext : public S5AppCtx
{
public:
	std::set<S5Connection*> ingoing_connections;
	std::string mngt_ip;
	int store_id;
    S5ZkClient zk_client;
    int64_t meta_size;

	S5TcpServer* tcp_server;
	std::vector<S5FlashStore*> trays;
	std::vector<S5Dispatcher*> disps;
	std::vector<S5Replicator*> replicators;

	pthread_mutex_t lock;
	std::map<uint64_t, S5Volume*> opened_volumes;

	S5Volume* get_opened_volume(uint64_t vol_id);
	int get_ssd_index(std::string ssd_uuid);
	S5AfsAppContext();
};
extern S5AfsAppContext app_context;
#endif

