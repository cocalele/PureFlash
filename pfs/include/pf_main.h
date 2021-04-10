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
#define IO_POOL_SIZE 4096

#define DATA_PORT 0
#define REP_PORT 1

#define COW_OBJ_SIZE (128<<10)
#define RECOVERY_IO_SIZE (128<<10) //recovery read IO size
#define DEFAULT_OBJ_SIZE (64<<20)
#define DEFAULT_OBJ_SIZE_ORDER 26 // DEFAULT_OBJ_SIZE=1<<DEFAULT_OBJ_SIZE_ORDER
//STATIC_ASSERT(DEFAULT_OBJ_SIZE == (1<<DEFAULT_OBJ_SIZE_ORDER));

class PfVolume;
class PfAfsAppContext : public PfAppCtx
{
public:
	std::set<PfConnection*> ingoing_connections;
	std::string mngt_ip;
	int store_id;
	PfZkClient zk_client;
	int64_t meta_size;
	int rep_conn_type;

	PfTcpServer* tcp_server;
	PfRdmaServer* rdma_server;
	struct PfRdmaDevContext* dev_ctx[4];
	std::vector<PfFlashStore*> trays;
	std::vector<PfDispatcher*> disps;
	std::vector<PfReplicator*> replicators;

	pthread_mutex_t lock;
	std::map<uint64_t, PfVolume*> opened_volumes;
    PfErrorHandler* error_handler;

	PfVolume* get_opened_volume(uint64_t vol_id);
	int get_ssd_index(std::string ssd_uuid);
	PfAfsAppContext();

	PfDispatcher *get_dispatcher(uint64_t vol_id);

	BigMemPool cow_buf_pool;
	BigMemPool recovery_buf_pool;
	BufferPool recovery_io_bd_pool;

	BackgroundTaskManager bg_task_mgr;
};
extern PfAfsAppContext app_context;
#endif

