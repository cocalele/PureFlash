/**
 * Copyright (C), 2014-2015.
 * @file  
 * 
 * This file implements the apis to initialize/release this server, and the functions to handle request messages.
 */

#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#include "s5errno.h"
#include "afs_server.h"
#include "s5utils.h"
#include "afs_main.h"
#include "afs_request.h"
#include "s5conf.h"
#include "spy.h"
#include "afs_flash_store.h"

extern struct afsc_st afsc;
static void *s5d_listentoe_thread(void *param);
static int handle_msg(s5v_msg_entry_t *s5vmsg);

static int parse_s5daemon_conf_file_afs_info(struct toedaemon* toe_daemon, conf_file_t fp)
{
	int rc = -1;	

	int nic_port_count = -1;
	rc = conf_get_int(fp, "afs", "max_nic_port_count", &nic_port_count);
	if(rc)
	{
		S5LOG_ERROR("Failed to find key(max_nic_port_count) in S5 daemon conf(%s).",
					toe_daemon->s5daemon_conf_file);
		return rc;
	}
    S5LOG_INFO("Parse_s5daemon_conf_file_afs_info:: get nic port count(%d) in S5 daemon conf(%s)", 
				nic_port_count, toe_daemon->s5daemon_conf_file);
	toe_daemon->nic_port_count = nic_port_count;

   	toe_daemon->daemon_request_port = LOCAL_HOST_PORT;

	int tray_set_count = -1; 
    rc = conf_get_int(fp, "afs", "max_tray_set_count", &tray_set_count);
    if(rc)
    {
        S5LOG_ERROR("Failed to find key(max_tray_set_count) in S5 daemon conf(%s).",
					toe_daemon->s5daemon_conf_file);
        return rc;
    }
    
    S5LOG_INFO("Parse_s5daemon_conf_file:: get tray set count(%d) in S5 daemon conf(%s)",  
                tray_set_count, toe_daemon->s5daemon_conf_file);
	toe_daemon->tray_set_count = tray_set_count;

	rc = flash_store_config(toe_daemon, fp);

	return rc;
}

static int create_toe_sockets_and_threads(struct toedaemon* toe_daemon)
{
    int rc = -1;
	for(int i = 0; i < toe_daemon->real_nic_count; i++)
	{
		for(int j = 0; j < toe_daemon->nic_port_count; j++)
		{
			int total_index = i * toe_daemon->nic_port_count + j;
		
			//should get front_ip and front_port from s5_conf.
			afsc.srv_toe_bd[total_index].socket = s5socket_create(SOCKET_TYPE_SRV, 
                                                                  afsc.srv_toe_bd[total_index].listen_port, 
                                                                  afsc.srv_toe_bd[total_index].listen_ip);
			afsc.srv_toe_bd[total_index].socket_clt = NULL;
			if(!afsc.srv_toe_bd[total_index].socket)
			{   
				rc = -S5_BIND_ERR;
				S5LOG_ERROR("Failed: init_s5d_srvbd:: srv_bd->socket error rc(%d). ", rc);
				return rc;  
			}  
	
			rc = pthread_create(&(afsc.srv_toe_bd[total_index].listen_s5toe_thread), NULL, 
				 				s5d_listentoe_thread, (void*)&(afsc.srv_toe_bd[total_index]));
    		if(rc)
    		{   
        		S5LOG_ERROR("Failed to create listen thread failed rc(%d) for ip(%s) port(%d).", 
							rc, afsc.srv_toe_bd[total_index].listen_ip, 
							afsc.srv_toe_bd[total_index].listen_port);
				return rc;
    		}   
			else
			{   
				S5LOG_INFO("Create listen thread successfully.");
			}

			
			s5socket_register_handle(afsc.srv_toe_bd[total_index].socket, SOCKET_TYPE_SRV, MSG_TYPE_WRITE, 
									 recv_msg_write, (void *)&(afsc.srv_toe_bd[total_index]));
			
			s5socket_register_handle(afsc.srv_toe_bd[total_index].socket, SOCKET_TYPE_SRV, MSG_TYPE_READ, 
									 recv_msg_read, (void *)&(afsc.srv_toe_bd[total_index]));
		}   
	}

	return rc;
}
	


static int parse_s5daemon_conf_file_nic_info(struct toedaemon* toe_daemon, conf_file_t fp)
{
	int rc = 0;

	const char*	ip_srv_bd = NULL;	
	toe_daemon->real_nic_count = 0;

	//get host port ip
	char host_port_setion[16][64] = {{0}};
	for(int index = 0; index < 16; index++)
	{    
		snprintf(host_port_setion[index], 64, "%s%d", (char*)g_host_port_section, index); 
		ip_srv_bd = conf_get(fp, host_port_setion[index], (char*)g_host_port_ip_key);
		if(!ip_srv_bd)
		{
			S5LOG_WARN("Failed to find key(%s) in S5 daemon conf(%s)", 
						g_host_port_ip_key, toe_daemon->s5daemon_conf_file);
			continue;
		}
		else
		{
			toe_daemon->real_nic_count++;
		}
	}

	int total_nic_port = toe_daemon->real_nic_count * toe_daemon->nic_port_count + 1;
	afsc.srv_toe_bd = (struct s5d_srv_toe *)malloc(sizeof(struct s5d_srv_toe) * (size_t)(total_nic_port));
	memset(afsc.srv_toe_bd, 0, sizeof(struct s5d_srv_toe) *(size_t)total_nic_port);
	afsc.ip_array = (uint32_t*)malloc(sizeof(uint32_t) * (uint32_t)toe_daemon->real_nic_count);

	if(!afsc.srv_toe_bd)
	{   
		S5LOG_ERROR("Failed malloc for toedaemon.");
		rc = -ENOMEM;
		return rc;
	}

	int nic_index = 0;
	for(int index = 0; index < 16; index++)
	{
		ip_srv_bd = conf_get(fp, host_port_setion[index], (char*)g_host_port_ip_key);
        if(!ip_srv_bd)
        {   
            continue;
        }   
        else
        {   
            S5LOG_INFO("Parse_s5daemon_conf_file_nic_info:: open the host port ip(%s) in S5 daemon conf(%s)", 
                       ip_srv_bd, toe_daemon->s5daemon_conf_file);
        } 

		afsc.ip_array[nic_index] = ntohl(inet_addr(ip_srv_bd)); 

		nic_index++;

	    int total_index = 0;
		for(int port_off = 0; port_off < toe_daemon->nic_port_count; port_off++)
		{
			total_index = (nic_index - 1) * toe_daemon->nic_port_count + port_off;
			afsc.srv_toe_bd[total_index].listen_ip = (char*)ip_srv_bd; 
			afsc.srv_toe_bd[total_index].listen_port = (unsigned short)(port_off + PORT_BASE);
		}
	}
	
	return rc;
}

int init_s5d_srv_toe(struct toedaemon* toe_daemon)
{
    int rc = -1; 

    // get s5daemon config file
    conf_file_t fp = NULL;
    fp = conf_open((char*)toe_daemon->s5daemon_conf_file);
    if(!fp)
    {
        S5LOG_ERROR("Failed to find S5afs conf(%s)", toe_daemon->s5daemon_conf_file);
        rc = -S5_CONF_ERR;
        return rc;
    }

	rc = parse_s5daemon_conf_file_afs_info(toe_daemon, fp);
    if(rc < 0)
    {   
    	goto EXIT;
	}   
        
    rc = parse_s5daemon_conf_file_nic_info(toe_daemon, fp);    

	if(rc < 0)
	{
		goto EXIT;
	}
    rc = s5socket_create_reaper_thread(&(toe_daemon->reap_socket_thread));
	if(rc < 0)
	{
		goto EXIT;
	}
	rc = create_toe_sockets_and_threads(toe_daemon);
	if(rc < 0)
	{
		goto EXIT;
	}


EXIT:
	if(fp)
		conf_close(fp);
	return rc;
}

int release_s5d_srv_toe(struct s5d_srv_toe* srv_toe_bd)
{
	int rc = -1;

	if(!srv_toe_bd)
	{
		S5LOG_INFO("Param is invalid.");
		rc = -EINVAL;
		return rc;
	}

	exit_thread(srv_toe_bd->listen_s5toe_thread);

    s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_WRITE);
    s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_READ);	
    s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_RGE_BLOCK_DELETE);
	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_S5_STAT);	
	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_NIC_CLIENT_INFO);

	s5socket_release(&srv_toe_bd->socket,SOCKET_TYPE_SRV);		
	return rc;
}

static int push_msg_to_handle_thread(PS5CLTSOCKET socket, s5_message_t* msg)
{
	int rc = 0;
	struct s5d_srv_toe*	srv_toe = NULL;
	s5v_msg_entry_t*	msg_entry = NULL;

	if(s5socket_get_handler_init_flag(socket) == FALSE)
	{
		s5socket_lock_handler_mutex(socket);
    	while(s5socket_get_handler_init_flag(socket) == FALSE)
    	{    
			s5socket_wait_cond(socket);
		}		
		s5socket_unlock_handler_mutex(socket);
	}

	srv_toe = s5socket_get_user_data(socket);
	if(!srv_toe)
	{
		S5LOG_ERROR("Failed to find s5d_srv_toe");
		rc = -EINVAL;
		return rc;
	}

	msg_entry = (s5v_msg_entry_t*)malloc(sizeof(s5v_msg_entry_t));
	if(!msg_entry)
	{
		S5LOG_ERROR("Failed to malloc msg_entry.");
		rc = -ENOMEM;
		return rc;
	}

	//put msg to handle entry.
	msg_entry->msg_list_entry.head = NULL;
	msg_entry->msg = msg;
	msg_entry->socket = socket;
	s5list_lock(&srv_toe->msg_list_head);
	s5list_push_ulc(&msg_entry->msg_list_entry, &srv_toe->msg_list_head);
	s5list_signal_entry(&srv_toe->msg_list_head);
	s5list_unlock(&srv_toe->msg_list_head);
	return rc;
}

int recv_msg_read(void* sockParam, s5_message_t* msg, void* param)
{
	int rc = -1;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	rc = push_msg_to_handle_thread(socket, msg);
	if(rc)
	{
		rc = cachemgr_read_request(msg, socket);
		s5msg_release_all(&msg);		
	}

	return rc;
}

int recv_msg_write(void* sockParam, s5_message_t* msg, void* param)
{
	int rc = -1;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;


	rc = push_msg_to_handle_thread(socket, msg);
	if(rc)
	{
		rc = cachemgr_write_request(msg, socket);
		s5msg_release_all(&msg);		
	}

	return rc;
}


int recv_msg_block_delete_request(void* sockParam, s5_message_t* msg, void* param)
{
	int rc = -1;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;

	if(!socket || !msg)
	{
		S5LOG_ERROR("Failed: param is invalid.");
		return -EINVAL;
	}

	rc = push_msg_to_handle_thread(socket, msg);
	if(rc)
	{
		rc = cachemgr_block_delete_request(msg, socket);
		s5msg_release_all(&msg);	
	}

	return rc;
}

int recv_msg_s5_stat_request(void* sockParam, s5_message_t* msg, void* param)
{
	int rc = -1;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;

    if(!socket || !msg)
    {   
        S5LOG_ERROR("Failed: param is invalid.");
        return -EINVAL;
    }   

    rc = push_msg_to_handle_thread(socket, msg);
    if(rc)
    {   
		S5ASSERT("Not supported, use spy" == 0);
        s5msg_release_all(&msg);    
    }   

    return rc; 	
}

static int handle_socket_exception(void* clntSock, void* srv_toe)
{
    struct s5d_srv_toe* s5_srv_toe = (struct s5d_srv_toe*)srv_toe;
    s5v_msg_entry_t* msg_entry = (s5v_msg_entry_t*)malloc(sizeof(s5v_msg_entry_t));;
    msg_entry->msg_list_entry.head = NULL;
    msg_entry->msg = NULL;
    msg_entry->socket = NULL;

    s5list_lock(&s5_srv_toe->msg_list_head);
    s5_srv_toe->exitFlag = 1;
    s5list_push_ulc(&msg_entry->msg_list_entry, &s5_srv_toe->msg_list_head);
    s5list_signal_entry(&s5_srv_toe->msg_list_head);
    s5list_unlock(&s5_srv_toe->msg_list_head);  
	s5socket_unregister_conn_exception_handle(clntSock);
    return 0;
}
static void *msg_process_thread(void *param)
{
	struct s5d_srv_toe* srv_toe = (struct s5d_srv_toe*)param;
	s5_dlist_entry_t *entry = NULL;
	int need_handle = 0;

	while (!srv_toe->exitFlag)
	{
		s5v_msg_entry_t *s5vmsg = NULL;

		//get msg
		s5list_lock(&srv_toe->msg_list_head);

		need_handle = srv_toe->msg_list_head.count;
		if (need_handle == 0)
		{
			s5list_wait_entry(&srv_toe->msg_list_head);
		}

		entry = s5list_poptail_ulc(&srv_toe->msg_list_head);
		if (!entry)
		{
			s5list_unlock(&srv_toe->msg_list_head);
			continue;
		}

		s5vmsg = S5LIST_ENTRY(entry, s5v_msg_entry_t, msg_list_entry);
		s5list_unlock(&srv_toe->msg_list_head);

		//handle s5vmsg.
		if (s5vmsg->msg)
		{
			handle_msg(s5vmsg);
			s5msg_release_all(&(s5vmsg->msg));
		}

		free(s5vmsg);
		s5vmsg = NULL;
	}
	return NULL;
}

void *s5d_listentoe_thread(void *param)
{
	pthread_t threadID;
	pthread_t msgThreadID;
	PS5CLTSOCKET clntSock = NULL;
	int rc = 0;
	struct s5d_srv_toe* srv_toe = (struct s5d_srv_toe*)param;	
	if(!srv_toe)
	{
		S5LOG_ERROR("Failed:  prama is invalid,thread exit.");
		return NULL;
	}

	threadID = pthread_self();
	S5LOG_INFO("Listen Thread:  listen_ip(srv_toe->listen_ip(%s) listen port(%u) thread(%llu) start.",
			   srv_toe->listen_ip, srv_toe->listen_port, (unsigned long long)threadID);
	
	s5list_init_head(&srv_toe->msg_list_head);

	rc = pthread_create(&msgThreadID, NULL,	msg_process_thread, param);

	S5LOG_INFO("Listen Thread: ip(%s) port(%u) is available now.",
		srv_toe->listen_ip, srv_toe->listen_port);

	while (!srv_toe->exitFlag)
	{
		clntSock = s5socket_accept(srv_toe->socket, 1);
		if (clntSock <= 0)
			continue;
		srv_toe->socket_clt = clntSock;
		s5socket_lock_handler_mutex(clntSock);

		s5list_clear(&srv_toe->msg_list_head);

		//s5socket_register_conn_exception_handle(clntSock, handle_socket_exception, srv_toe);
		srv_toe->exitFlag = 0;
		s5socket_set_user_data(clntSock, srv_toe);

		s5socket_set_handler_init_flag(clntSock, TRUE);
		s5socket_signal_cond(clntSock);
		s5socket_unlock_handler_mutex(clntSock);

		char* s5bd_ip = s5socket_get_foreign_ip(clntSock);
		int s5bd_port = s5socket_get_foreign_port(clntSock);
		S5LOG_INFO("S5afs:Accept s5bd<ip:%s port:%d> ", s5bd_ip, s5bd_port);
	}

	return NULL;
}

static int handle_msg(s5v_msg_entry_t *s5vmsg)
{
	int rc = 0;
	switch(s5vmsg->msg->head.msg_type)
	{
		case MSG_TYPE_READ:
			rc = cachemgr_read_request(s5vmsg->msg, s5vmsg->socket);
			break;
		case MSG_TYPE_WRITE:
			rc = cachemgr_write_request(s5vmsg->msg, s5vmsg->socket);
			break;
		case MSG_TYPE_RGE_BLOCK_DELETE:
			rc = cachemgr_block_delete_request(s5vmsg->msg, s5vmsg->socket);
			break;
		case MSG_TYPE_S5_STAT:
			S5ASSERT("Not supported, use spy" == 0);
			break;
		case MSG_TYPE_NIC_CLIENT_INFO: 
		default:
			break;
	}
	return	rc;
}

