/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
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

#include "pf_zk_client.h"
#include "pf_app_ctx.h"
#include "pf_flash_store.h"
#include "pf_replicator.h"
#include "pf_dispatcher.h"
#include "pf_error_handler.h"
#include "pf_bgtask_manager.h"


class PfTcpServer;
class PfRdmaServer;

#define MAX_TRAY_COUNT 32
#define MAX_PORT_COUNT 4
#define MAX_DISPATCHER_COUNT 10
#define MAX_REPLICATOR_COUNT 10
#define IO_POOL_SIZE 1024
//#define IO_POOL_SIZE 128

#define DATA_PORT 0
#define REP_PORT 1

//STATIC_ASSERT(DEFAULT_OBJ_SIZE == (1<<DEFAULT_OBJ_SIZE_ORDER));

extern char const_zero_page[4096];

class PfVolume;
class PfAfsAppContext : public PfAppCtx
{
public:
	std::map<uintptr_t, PfConnection*> client_ip_conn_map;
	std::mutex conn_map_lock;

	std::string mngt_ip;
	int store_id;
	PfZkClient zk_client;
	int64_t meta_size;
	int rep_conn_type;

	PfTcpServer* tcp_server;
	PfRdmaServer* rdma_server;
	std::vector<PfFlashStore*> trays;
	std::vector<PfDispatcher*> disps;
	std::vector<PfReplicator*> replicators;

	//int dis_index;

	pthread_mutex_t lock;
	std::map<uint64_t, PfVolume*> opened_volumes;
    PfErrorHandler* error_handler;


	BigMemPool recovery_buf_pool;
	BufferPool recovery_io_bd_pool;

	BackgroundTaskManager bg_task_mgr;
	int next_client_disp_id; //to assign shared client connection to dispatcher
	std::thread cron_thread;

	PfVolume* get_opened_volume(uint64_t vol_id);
	int get_ssd_index(std::string ssd_uuid);
	int PfRdmaRegisterMr(struct PfRdmaDevContext *dev_ctx);
	void PfRdmaUnRegisterMr();
	PfAfsAppContext();

	PfDispatcher *get_dispatcher();
	void remove_connection(PfConnection* _conn);
	void add_connection(PfConnection* _conn);

	int next_shard_replicator_id; //to assign volume shard to replicator
	PfReplicator *get_replicator();
};
extern PfAfsAppContext app_context;
#endif

