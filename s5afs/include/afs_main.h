/**
 * Copyright (C), 2014-2015.
 * @file  
 *    
 * This file defines the data structure: toedaemon, and defines its initialization and release func.
 */

#ifndef __TOEDAEMON__
#define __TOEDAEMON__

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <set>

#include "s5_app_ctx.h"

class S5TcpServer;

class S5AfsAppContext : public S5AppCtx
{
public:
	std::set<S5Connection*> ingoing_connections;
	std::string mngt_ip;

	S5TcpServer* tcp_server;
};
extern S5AfsAppContext app_context;
#endif	

