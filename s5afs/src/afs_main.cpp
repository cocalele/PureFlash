/**
 * Copyright (C), 2019.
 *
 * @file
 * This file defines toe server initialization and release func.
 */
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "cmdopt.h"
#include "s5_utils.h"
#include "afs_server.h"
#include "s5errno.h"
#include "afs_cluster.h"
#include "s5_app_ctx.h"

S5AfsAppContext app_context;

void sigroutine(int dunno)
{
    switch(dunno)
    {
        case SIGTERM:
            S5LOG_INFO("Recieve signal SIGTERM.");
            exit(1);

        case SIGINT:
            S5LOG_INFO("Recieve signal SIGINT.");
            exit(1);
    }
    return;
}



static void printUsage()
{
	S5LOG_ERROR("Usage: s5afs -c <s5daemon_conf_file> [-d] [-sp spy_port]\n\t\t-d daemon mode.");
}



int main(int argc, char *argv[])
{
	int rc = -1;
	const char*	s5daemon_conf = NULL;

    S5LOG_INFO("Toedaemon start.");
 	if (argc < 3)
	{
		printUsage();
		rc = -EINVAL;
		S5LOG_ERROR("Failed: param is not enough to start argc(%d).", argc);
		return rc;
	}

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
    fp = conf_open(s5daemon_conf);
    if(!fp)
    {
        S5LOG_ERROR("Failed to find S5afs conf(%s)", s5daemon_conf);
        return -S5_CONF_ERR;
    }
    const char *zk_ip = conf_get(fp, "zookeeper", "ip", NULL, true);
    if(!zk_ip)
    {
        S5LOG_ERROR("Failed to find key(zookeeper:ip) in s5afs conf(%s).", s5daemon_conf);
        return -S5_CONF_ERR;
    }


	const char *this_mngt_ip = conf_get(fp, "afs", "mngt_ip", NULL, true);
	if (!this_mngt_ip)
	{
		S5LOG_ERROR("Failed to find key(conductor:mngt_ip) in s5afs conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	app_context.mngt_ip = this_mngt_ip;
    rc = init_cluster(zk_ip);
    if (rc)
    {
        S5LOG_ERROR("Failed to connect zookeeper");
        return rc;
    }
    rc = register_store_node(this_mngt_ip);
    if (rc)
    {
        S5LOG_ERROR("Failed to register store");
        return rc;
    }

    app_context.tcp_server=new S5TcpServer();
    rc = app_context.tcp_server->init();
    if(rc)
    {
    	S5LOG_ERROR("Failed to init tcp server:%d", rc);
    	return rc;
    }
	set_store_node_state(this_mngt_ip, NS_OK, TRUE);
    signal(SIGTERM, sigroutine);
    signal(SIGINT, sigroutine);

	while(!sleep(1));


FINALLY:
    S5LOG_INFO("toe_daemon exit.");
    return rc;
}

