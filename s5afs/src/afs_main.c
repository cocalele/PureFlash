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

int init_src(struct toedaemon* toe_daemon)
{
	int rc = -1;
	if(!toe_daemon)
	{
		goto FINALLY;
	}

	rc = init_s5d_srv_toe(toe_daemon);	
	if (rc != 0)
	{
		S5LOG_ERROR("Failed: init_s5d_srvtoe failed rc(%d).", rc);
		return rc;
	}
	
	cachemgr_init(toe_daemon->tray_set_count, toe_daemon->nic_port_count);
	

FINALLY:	
	return rc;
}

int release_src(struct toedaemon *toe_daemon)
{
	int rc = -1;
	int i;
	int index = -1;
	char text_name[1024];

	if(!toe_daemon)
	{
		S5LOG_ERROR("Failed: param is invalid.");
		rc = -EINVAL;
		goto FINALLY;
	}

	for(i = 0; i < toe_daemon->real_nic_count; i++)
	{
		for(int j = 0; j < toe_daemon->nic_port_count; j++)
		{
			index = i * toe_daemon->nic_port_count + j;
			release_s5d_srv_toe(&(afsc.srv_toe_bd[index]));
		}		
	}

	if(toe_daemon->real_nic_count > 0)
	{
		index = toe_daemon->real_nic_count * toe_daemon->nic_port_count;
		release_s5d_srv_toe(&(afsc.srv_toe_bd[index]));	

		rc = exit_thread(toe_daemon->reap_socket_thread);
	}

	for(i = 0; i < toe_daemon->tray_set_count; i++)
	{
		snprintf(text_name,1024,"raw_capacity_%i",i);
		spy_unregister(text_name);
	}	

	spy_unregister("read");
	spy_unregister("write");
	spy_unregister("reply_ok");
	spy_unregister("delete");
	spy_unregister("stat");
	spy_unregister("client_info");
	spy_unregister("total_raw_capacity");
	spy_unregister("total_avail_capacity");

	free(afsc.srv_toe_bd);
FINALLY:	
	return rc;	
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
	
	toe_daemon->s5daemon_conf_file = s5daemon_conf;	

	rc = init_src(toe_daemon);
	if(rc)
	{
		S5LOG_ERROR("Failed to init_src rc:%d.", rc);
		goto RELEASE;
	}
	if(spy_port)
	{
		spy_start(spy_port);
	}

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
	rc = release_src(toe_daemon);
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

