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

#include "s5_zk_client.h"
#include "s5_app_ctx.h"
#include "afs_flash_store.h"

class S5TcpServer;

#define MAX_TRAY_COUNT 32

class S5AfsAppContext : public S5AppCtx
{
public:
	std::set<S5Connection*> ingoing_connections;
	std::string mngt_ip;
    S5ZkClient zk_client;

	S5TcpServer* tcp_server;
	std::vector<S5FlashStore*> trays;
};
extern S5AfsAppContext app_context;
#endif	

