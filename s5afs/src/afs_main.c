/**
 * Copyright (C), 2014-2015.
 * @file  
 *    
 * This file defines toe server initialization and release func.
 */
#include <signal.h>
#include <unistd.h>
#include "cmdopt.h"
#include "afs_request.h"
#include "s5utils.h"
#include "spy.h"
#include "afs_server.h"
#include "s5errno.h"
#include "afs_cluster.h"

S5LOG_INIT("s5afs")

struct afsc_st afsc;

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


int release_afs(struct toedaemon *toe_daemon)
{
	release_store_server(toe_daemon);
	free(afsc.srv_toe_bd);
	return 0;	
}

static void printUsage()
{
	S5LOG_ERROR("Usage: s5afs -c <s5daemon_conf_file> [-d] [-sp spy_port]\n\t\t-d daemon mode.");
}



int main(int argc, char *argv[])
{
	BOOL daemon_mode = FALSE;
	int rc = -1;
	int spy_port = 2000;

	const char*	s5daemon_conf = NULL;

	struct toedaemon* toe_daemon = NULL;
    S5LOG_INFO("Toedaemon start.");
 	if (argc < 3)  
	{ 	
		printUsage();
		rc = -EINVAL;
		S5LOG_ERROR("Failed: param is not enough to start argc(%d).", argc);
		return rc;
	}

	opt_initialize(argc, (const char**)argv);
	while(opt_error_code() == 0 && opt_has_next())
	{
		const char* name = opt_next();
		if(strcmp(name, "c") == 0)
		{
			s5daemon_conf = opt_value();
		}
		else if(strcmp(name, "d") == 0)
		{
			daemon_mode = 1;
		}
		else if(strcmp(name, "sp") == 0)
		{
			spy_port = opt_value_as_int();
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
	if(daemon_mode)
	{
		daemon(1, 0);
	}

	write_pid_file("s5afs.pid");
	toe_daemon = (struct toedaemon*)malloc(sizeof(struct toedaemon));
	if(!toe_daemon)
	{
		S5LOG_ERROR("Failed to malloc for toedaemon.");
		rc = -ENOMEM;
		goto FINALLY;
	}
	else
	{
		memset(toe_daemon, 0, sizeof(*toe_daemon));
	}
	
    conf_file_t fp = NULL;
    fp = conf_open(s5daemon_conf);
    if(!fp)
    {
        S5LOG_ERROR("Failed to find S5afs conf(%s)", s5daemon_conf);
        return -S5_CONF_ERR;
    }
    const char *zk_ip = conf_get(fp, "zookeeper", "ip");
    if(!zk_ip)
    {
        S5LOG_ERROR("Failed to find key(zookeeper:ip) in s5afs conf(%s).", s5daemon_conf);
        return -S5_CONF_ERR;
    }
	
	
	const char *this_mngt_ip = conf_get(fp, "afs", "mngt_ip");
	if (!this_mngt_ip)
	{
		S5LOG_ERROR("Failed to find key(conductor:mngt_ip) in s5afs conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	safe_strcpy(toe_daemon->mngt_ip, this_mngt_ip, sizeof(toe_daemon->mngt_ip));
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
	toe_daemon->s5daemon_conf_file = s5daemon_conf;	

	rc = init_store_server(toe_daemon);
	if(rc)
	{
		S5LOG_ERROR("Failed on init_store_server rc:%d.", rc);
		goto RELEASE;
	}
	if(spy_port)
	{
		spy_start(spy_port);
	}
	set_store_node_state(this_mngt_ip, NS_OK, TRUE);
    signal(SIGTERM, sigroutine);
    signal(SIGINT, sigroutine);

	char ch;
	if(!daemon_mode)
	{
		do {
			S5LOG_INFO("Input 'x' to exit s5sdaemon.");
			ch=(char)getchar();
		} while(ch != 'x');
		S5LOG_INFO("You have input '%c', exit.", ch);
	}
	else
	{
		while(!sleep(1));
	}

RELEASE:
	rc = release_afs(toe_daemon);
	if(rc)
		S5LOG_INFO("Error:release_src rc:%d.", rc);

	if(toe_daemon)
	{
		free(toe_daemon);
		toe_daemon = NULL;
	}
	remove("afs.pid");
FINALLY:
    S5LOG_INFO("toe_daemon exit.");	
    return rc;
}

