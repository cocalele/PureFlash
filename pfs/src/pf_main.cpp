/**
 * Copyright (C), 2019.
 *
 * @file
 * This file defines toe server initialization and release func.
 */
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include <execinfo.h>
#include <pf_message.h>

#include "cmdopt.h"
#include "pf_utils.h"
#include "pf_server.h"
#include "pf_errno.h"
#include "pf_cluster.h"
#include "pf_app_ctx.h"
#include "pf_message.h"
using namespace std;
int init_restful_server();
void unexpected_exit_handler();
void stop_app();
PfAfsAppContext app_context;

void sigroutine(int dunno)
{
	switch(dunno)
	{
		case SIGTERM:
			S5LOG_INFO("Recieve signal SIGTERM.");
			exit(1);

		case SIGINT:
			S5LOG_INFO("Recieve signal SIGINT.");
			stop_app();
			exit(0);
	}
	return;
}



static void printUsage()
{
	S5LOG_ERROR("Usage: pfs -c <pfs_conf_file>");
}



int main(int argc, char *argv[])
{
	int rc = -1;
	const char*	s5daemon_conf = NULL;

	S5LOG_INFO("================================================");
	S5LOG_INFO("====       ___               ___            ====");
	S5LOG_INFO("====      (o o)             (o o)           ====");
	S5LOG_INFO("====     (  V  ) PureFlash (  V  )          ====");
	S5LOG_INFO("====     --m-m---------------m-m--          ====");
	S5LOG_INFO("PureFlash pfs start..., version:1.0 build:%s %s", __DATE__, __TIME__);
	std::set_terminate(unexpected_exit_handler);
	g_app_ctx = &app_context;
	opt_initialize(argc, (const char**)argv);
	while(opt_error_code() == 0 && opt_has_next())
	{
		const char* name = opt_next();
		if(strcmp(name, "c") == 0)
		{
			s5daemon_conf = opt_value();
		}
		else
		{
			S5LOG_ERROR("Failed: Invalid argument \"%s\"", name);
			printUsage();
			opt_uninitialize();
			return -EINVAL;;
		}
	}
	if(opt_error_code() != 0)
	{
		S5LOG_ERROR("Failed: error_code=%s", opt_error_message());
		printUsage();
		opt_uninitialize();
		return -EINVAL;
	}
	opt_uninitialize();

	conf_file_t fp = NULL;
	if(s5daemon_conf == NULL)
		s5daemon_conf = "/etc/pureflash/pfs.conf";
	fp = conf_open(s5daemon_conf);
	if(!fp)
	{
		S5LOG_FATAL("Failed to find S5afs conf(%s)", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	app_context.conf = fp;
	const char *zk_ip = conf_get(fp, "zookeeper", "ip", NULL, true);
	if(!zk_ip)
	{
		S5LOG_FATAL("Failed to find key(zookeeper:ip) in conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}


	const char *this_mngt_ip = conf_get(fp, "afs", "mngt_ip", NULL, true);
	if (!this_mngt_ip)
	{
		S5LOG_FATAL("Failed to find key(afs:mngt_ip) in conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	const char *cluster_name = conf_get(fp, "cluster", "name", NULL, true);
	if (!cluster_name)
	{
		S5LOG_FATAL("Failed to find key(cluster:name) in conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	app_context.mngt_ip = this_mngt_ip;
	rc = init_cluster(zk_ip, cluster_name);
	if (rc)
	{
		S5LOG_ERROR("Failed to connect zookeeper");
		return rc;
	}

	int store_id = conf_get_int(fp, "afs", "id", 0, TRUE);
	if(store_id == 0)
	{
		S5LOG_FATAL("afs.id not defined in conf file");
	}
	app_context.store_id = store_id;
	S5LOG_INFO("Register store to ZK.");
	rc = register_store_node(store_id, this_mngt_ip);
	if (rc)
	{
		S5LOG_ERROR("Failed to register store");
		return rc;
	}
    app_context.meta_size = conf_get_long(fp, "afs", "meta_size", META_RESERVE_SIZE, FALSE);
	if(app_context.meta_size < MIN_META_RESERVE_SIZE)
		S5LOG_FATAL("meta_size in config file is too small, at least %ld", MIN_META_RESERVE_SIZE);
	int i=0;
	for(i=0;i<MAX_TRAY_COUNT;i++)
	{
		string name = format_string("tray.%d", i);
		const char* devname = conf_get(fp, name.c_str(), "dev", NULL, false);
		if(devname == NULL)
			break;
		auto s = new PfFlashStore();
		rc = s->init(devname);
		if(rc)
		{
			S5LOG_ERROR("Failed init tray:%s, rc:%d", devname, rc);
			continue;
		}
		else
		{
			app_context.trays.push_back(s);
		}
		register_tray(store_id, s->head.uuid, s->tray_name, s->head.tray_capacity);
		s->start();
	}
	for (i = 0; i < MAX_PORT_COUNT; i++)
	{
		string name = format_string("port.%d", i);
		const char* ip = conf_get(fp, name.c_str(), "ip", NULL, false);
		if (ip == NULL)
			break;
		rc = register_port(store_id, ip, DATA_PORT);
		if(rc) {
			S5LOG_ERROR("Failed register port:%s, rc:%d", ip, rc);
			continue;
		}

	}

	for (i = 0; i < MAX_PORT_COUNT; i++)
	{
		string name = format_string("rep_port.%d", i);
		const char* ip = conf_get(fp, name.c_str(), "ip", NULL, false);
		if (ip == NULL)
			break;
		rc = register_port(store_id, ip, REP_PORT);
		if(rc) {
			S5LOG_ERROR("Failed register port:%s, rc:%d", ip, rc);
			continue;
		}

	}
	int disp_count = conf_get_int(app_context.conf, "dispatch", "count", 4, FALSE);
	app_context.disps.reserve(disp_count);
	for(int i=0;i<disp_count; i++)
	{
		app_context.disps.push_back(new PfDispatcher());
		rc = app_context.disps[i]->init(i);
		if(rc) {
			S5LOG_ERROR("Failed init dispatcher[%d], rc:%d", i, rc);
			return rc;
		}
		rc = app_context.disps[i]->start();
		if(rc != 0) {
			S5LOG_FATAL("Failed to start dispatcher, index:%d", i);
		}
	}

	int rep_count = conf_get_int(app_context.conf, "replicator", "count", 2, FALSE);
	app_context.replicators.reserve(rep_count);
	for(int i=0; i< rep_count; i++) {
		PfReplicator* rp = new PfReplicator();
		rc = rp->init(i);
		if(rc) {
			S5LOG_ERROR("Failed init replicator[%d], rc:%d", i, rc);
			return rc;
		}
		app_context.replicators.push_back(rp);
		rc = rp->start();
		if(rc != 0) {
			S5LOG_FATAL("Failed to start replicator, index:%d", i);
		}
	}

	app_context.tcp_server=new PfTcpServer();
	rc = app_context.tcp_server->init();
	if(rc)
	{
		S5LOG_ERROR("Failed to init tcp server:%d", rc);
		return rc;
	}
	do {
		rc = set_store_node_state(store_id, NS_OK, TRUE);
		if(rc == ZNODEEXISTS) {
			S5LOG_WARN("alive node already exists, may caused by duplicated pfs service or previous abnormal exit");
			sleep(1);
		}
	}while(rc == ZNODEEXISTS);
	signal(SIGTERM, sigroutine);
	signal(SIGINT, sigroutine);
	init_restful_server(); //never return
	while(sleep(1) == 0);

	S5LOG_INFO("toe_daemon exit.");
	return rc;
}

int PfAfsAppContext::get_ssd_index(std::string ssd_uuid)
{
	for(int i=0;i<trays.size();i++)
	{
		char uuid_str[64];
		uuid_unparse(trays[i]->head.uuid, uuid_str);

		if (ssd_uuid == uuid_str)
			return i;
	}
	S5LOG_ERROR("Not found disk:%s", ssd_uuid.c_str());
	return -1;
}

PfAfsAppContext::PfAfsAppContext() : cow_buf_pool(COW_OBJ_SIZE)
{
	pthread_mutex_init(&lock, NULL);
	error_handler = new PfErrorHandler();
}

PfVolume* PfAfsAppContext::get_opened_volume(uint64_t vol_id)
{
	pthread_mutex_lock(&app_context.lock);
	DeferCall _c([]() {pthread_mutex_unlock(&app_context.lock);});
	auto pos = opened_volumes.find(vol_id);
	if (pos == opened_volumes.end())
		return NULL;
	return pos->second;
}

PfDispatcher *PfAfsAppContext::get_dispatcher(uint64_t vol_id) {
	return disps[VOL_ID_TO_VOL_INDEX(vol_id)%disps.size()];
}

void unexpected_exit_handler()
{
/*		try { throw; }
		catch(const std::exception& e) {
			S5LOG_ERROR("Unhandled exception:%s", e.what());
		}
		catch(...) {
			S5LOG_ERROR("Unexpected exception");
		}
		*/
	S5LOG_ERROR("unexpected_exit_handler");

    void *trace_elems[20];
    int trace_elem_count(backtrace( trace_elems, 20 ));
    char **stack_syms(backtrace_symbols( trace_elems, trace_elem_count ));
    for ( int i = 0 ; i < trace_elem_count ; ++i )
    {
		std::cout << stack_syms[i] << "\n";
	}
    free( stack_syms );

    exit(1);

}   

void stop_app()
{
	app_context.tcp_server->stop();
	for(int i=0;i<app_context.trays.size();i++)
	{
		app_context.trays[i]->save_meta_data();
		app_context.trays[i]->stop();
	}
}