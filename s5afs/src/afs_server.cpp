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
#include "s5_utils.h"
#include "afs_main.h"
#include "afs_request.h"
#include "s5conf.h"
#include "s5_buffer.h"
#include "afs_flash_store.h"
#include "s5_connection.h"

extern struct afsc_st afsc;
static void *afs_listen_thread(void *param);
static int handle_msg(s5v_msg_entry_t *s5vmsg);

static int init_trays(struct toedaemon* toe_daemon, conf_file_t fp)
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

static int release_trays(struct toedaemon* toe_daemon)
{
	return 0;
}

static int create_sockets_and_threads(struct toedaemon* toe_daemon)
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
				 				afs_listen_thread, (void*)&(afsc.srv_toe_bd[total_index]));
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



static int init_ports(struct toedaemon* toe_daemon, conf_file_t fp)
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
	rc = s5socket_create_reaper_thread(&(toe_daemon->reap_socket_thread));
	if (rc < 0)
	{
		return rc;
	}
	rc = create_sockets_and_threads(toe_daemon);
	if (rc < 0)
	{
		return rc;
	}

	return rc;
}

static int release_socket_and_thread(struct s5d_srv_toe* srv_toe_bd)
{
	int rc = 0;

	exit_thread(srv_toe_bd->listen_s5toe_thread);

	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_WRITE);
	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_READ);
	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_RGE_BLOCK_DELETE);
	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_S5_STAT);
	s5socket_unreigster_handle(srv_toe_bd->socket, SOCKET_TYPE_SRV, MSG_TYPE_NIC_CLIENT_INFO);

	s5socket_release(&srv_toe_bd->socket, SOCKET_TYPE_SRV);
	return rc;
}

static int release_ports(struct toedaemon* toe_daemon)
{
	int i;
	int rc = 0;
	for (i = 0; i < toe_daemon->real_nic_count; i++)
	{
		for (int j = 0; j < toe_daemon->nic_port_count; j++)
		{
			int index = i * toe_daemon->nic_port_count + j;
			release_socket_and_thread(&(afsc.srv_toe_bd[index]));
		}
	}

	if (toe_daemon->real_nic_count > 0)
	{
		int index = toe_daemon->real_nic_count * toe_daemon->nic_port_count;
		release_socket_and_thread(&(afsc.srv_toe_bd[index]));

		rc = exit_thread(toe_daemon->reap_socket_thread);
	}
	return rc;
}

int init_store_server(struct toedaemon* toe_daemon)
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

	rc = init_trays(toe_daemon, fp);
    if(rc < 0)
    {
    	goto EXIT;
	}

    rc = init_ports(toe_daemon, fp);
	if(rc < 0)
	{
		goto EXIT;
	}
	register_spy_variables();


EXIT:
	if(fp)
		conf_close(fp);
	return rc;
}
int release_store_server(struct toedaemon* toe_daemon)
{
	int rc = 0;
	unregister_spy_variables();
	rc = release_ports(toe_daemon);
	rc = release_trays(toe_daemon);
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

void *afs_listen_thread(void *param)
{
	((S5TcpServer*)param)->listen_proc();

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

int S5TcpServer::init(conf_file_t conf)
{
	int rc = 0;
	poller_cnt = conf_get_int(conf, "tcp_server", "poller_count", 4, FALSE);
	pollers = new S5Poller[poller_cnt];
	for(int i=0;i<poller_cnt;i++)
	{
		rc = pollers[i].init(512);
		if (rc != 0)
			S5LOG_FATAL("Failed init TCP pollers[%d], rc:%d", i, rc);
	}
	rc = pthread_create(&listen_s5toe_thread, NULL, afs_listen_thread, this);
	if (rc)
	{
		S5LOG_FATAL("Failed to create TCP listen thread failed rc:%d",rc);
		return rc;
	}
}

void S5TcpServer::listen_proc()
{
	int rc = 0;
	int yes = 1;
	sockaddr_in srv_addr;
	S5LOG_INFO("Init TCP server with IP:<NULL>:%d", TCP_PORT_BASE);
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(TCP_PORT_BASE);

	server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);;
	if (server_socket_fd < 0) {
		rc = -errno;
		S5LOG_FATAL("Failed to create TCP server socket, rc:%d", rc);
		goto release1;
	}
	rc = setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int));
	if (rc != 0)
	{
		rc = -errno;
		S5LOG_ERROR("set SO_REUSEPORT failed, rc:%d", rc);
	}
	rc = setsockopt(server_socket_fd, IPPROTO_TCP, TCP_QUICKACK, &yes, sizeof(int));
	if (rc != 0)
	{
		rc = -errno;
		S5LOG_ERROR("set TCP_QUICKACK failed, rc:%d", rc);
	}
	/* Now bind the host address using bind() call.*/
	if (bind(server_socket_fd, (struct sockaddr *) &srv_addr, sizeof(srv_addr)) < 0)
	{
		rc = -errno;
		S5LOG_FATAL("Failed to bind socket, rc:%d", rc);
		goto release2;
	}


	if (listen(server_socket_fd, 128) < 0)
	{
		rc = -errno;
		S5LOG_FATAL("Failed to listen socket, rc:%d", rc);
		goto release2;
	}

	while (1)
	{
		accept_connection();
	}
	return;
release2:
release1:
	return;
}
int on_tcp_handshake_recved(BufferDescriptor* bd, S5Connection* conn_, void* cbk_data)
{
	S5TcpConnection* conn = (S5TcpConnection*)conn_;
	struct qfa_client_volume_priv* volume;

	volume = (struct qfa_client_volume_priv*)calloc(1, sizeof(struct qfa_client_volume_priv));
	if (volume == NULL)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to malloc volume memory on connection from:%s", conn->connection_info.c_str());
		conn->close();
		return 0;
	}
	conn->state = CONN_OK;
	conn->volume = volume;
	return 0;
}
int S5TcpServer::accept_connection()
{
	sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);
	int rc = 0;
	int connfd = accept(server_socket_fd, &client_addr, &addr_len);

	if (connfd < 0) {
		S5LOG_ERROR("Failed to accept tcp connection, rc:%d", -errno);
		return -errno;
	}

	S5TcpConnection* conn = new S5TcpConnection();
	if (conn == NULL)
	{
		rc = -ENOMEM;
		S5LOG_ERROR("Failed to alloc qfa_connection for connection from:%s, rc:%d", ipstr, rc);
		goto release1;
	}
	rc = conn->init(connfd, get_best_poller(), 128, 128);
	//add this to debug bad performance in Wdindows driver
	conn_add_ref(conn); //decreased in `server_on_disconnect`
	__sync_fetch_and_add(&conn_stat.server_tcp_cnt, 1);
	conn->transport = TCP;
	conn->on_disconnected = server_on_disconnect;
	conn->on_release = server_on_conn_release;
	pthread_mutex_lock(&app_context.lock);
	conn->disp_index = app_context.next_dispatcher++;
	app_context.next_dispatcher %= app_context.dispatcher_count;
	conn->replicator_index = app_context.next_replicator++;
	app_context.next_replicator %= app_context.replicator_count;
	pthread_mutex_unlock(&app_context.lock);
	conn->rdma_poller_index = -1;
	conn->role = ROLE_SERVER;
	conn->transport = TCP;
	conn->state = CONN_INIT;

	BufferDescriptor *bd = new BufferDescriptor();
	bd->buf = new struct s5_handshake_message;
	bd->buf_size = sizeof s5_handshake_message;
	bd->data_len = 0;
	bd->on_work_complete = on_handshake_recved;
	conn->post_recv(bd);




	conn->heartbeat_receive_time = now_time_nsec();
	store_inst->res_collector->insert_conn(conn);
	return 0;
release10:
	store_inst->res_collector->remove_conn(conn);
	qfa_poller_del(tsession->poller, conn->ctrl_evt_queue.fd);
release9:
	qfa_release_event_queue(&conn->ctrl_evt_queue);
release8:
	qfa_poller_del(tsession->poller, tsession->send_q.fd);
release7:
	qfa_poller_del(tsession->poller, tsession->recv_q.fd);
release5:
	free(volume);
release4:
	qfa_release_tcp_session(tsession);
release3:
	free(tsession);
release2:
	free(conn);
release1:
	close(connfd);
	return rc;

	if (rc)
	{
		char* addrstr = inet_ntoa(client_addr.sin_addr);
		S5LOG_ERROR("Failed to create_tcp_server_connection for client:%s, rc:%d", addrstr, -errno);
		return -errno;
	}
	return 0;
}