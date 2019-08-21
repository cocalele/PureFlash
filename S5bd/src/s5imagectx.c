#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>           // For inet_addr()
#include <netinet/in.h>          // For sockaddr_in
#include <sys/types.h>
#include "internal.h"
#include "s5imagectx.h"
#include "s5_meta.h"
#include "s5message.h"
#include "spy.h"

#define free_s5io_queue_item(p) free(p)

int64 event_delta = 1;

int32 s5_volumectx_init(struct s5_volume_ctx* ictx)
{
	int r = EOK;

	ictx->node_slot_num = ictx->volume_size/S5_OBJ_LEN;
	if(ictx->volume_size%S5_OBJ_LEN)
	{
		++ictx->node_slot_num;
	}

	pthread_spin_init(&ictx->io_num_lock, 0);

	ictx->iops_per_GB = 0;

	r = init_id_generator((size_t)ictx->session_conf.s5_io_depth, &ictx->idg);
	if(r)
	{
		S5LOG_ERROR("Failed to init id_generator, ret(%d).", r);
		goto FINALLY;
	}

	r = s5_volumectx_slotlist_init(ictx);
	if(r != EOK)
	{
		goto RELEASE_ID_GENERAOR;
	}

	r = s5_volumectx_node_mtx_init(ictx);
	if(r != EOK)
	{
		goto RELEASE_SLOTLIST;
	}

	r = s5_volumectx_node_cache_init(ictx);
	if(r != EOK)
	{
		goto RELEASE_NODE_MTX;
	}
	else
	{
		goto FINALLY;
	}


RELEASE_NODE_MTX:

	s5_volumectx_node_mtx_release(ictx);

RELEASE_ID_GENERAOR:

	release_id_generator(ictx->idg);

RELEASE_SLOTLIST:

	s5_volumectx_slotlist_release(ictx);

FINALLY:
	return r;
}

int32 s5_volumectx_release(struct s5_volume_ctx* ictx)
{
	release_id_generator(ictx->idg);

	s5_volumectx_node_cache_release(ictx);
	s5_volumectx_node_mtx_release(ictx);
	s5_volumectx_slotlist_release(ictx);

	return 0;
}

int32 s5_volumectx_slotlist_init(struct s5_volume_ctx* ictx)
{
	int r = EOK;
	ictx->slotlist = (struct s5_blocknode**)malloc(sizeof(struct s5_blocknode*) * ictx->node_slot_num);
	if(ictx->slotlist == NULL)
	{
		r = -ENOMEM;
		goto FINALLY;
	}
	memset(ictx->slotlist, 0, sizeof(struct s5_blocknode*) * ictx->node_slot_num);


FINALLY:

	return r;
}

void s5_volumectx_slotlist_release(struct s5_volume_ctx* ictx)
{
	int i = 0;
	if(ictx->slotlist)
	{
		for(i = 0; i != ictx->node_slot_num; ++i)
		{
			if(ictx->slotlist[i])
			{
				s5_blocknode_release(ictx->slotlist[i]);
				free(ictx->slotlist[i]);
				ictx->slotlist[i] = NULL;
			}
		}
		free(ictx->slotlist);
		ictx->slotlist = NULL;
	}

	return;
}

int32 s5_volumectx_node_mtx_init(struct s5_volume_ctx* ictx)
{
	int r = EOK;
	int i = 0;
	
	if(ictx->node_slot_num > NODE_MTX_NUM)
	{
		ictx->node_mtx_num = NODE_MTX_NUM;
	}
	else
	{
		ictx->node_mtx_num = (uint16)ictx->node_slot_num;
	}
	S5ASSERT(ictx->node_mtx == NULL);
	ictx->node_mtx = (pthread_spinlock_t*)malloc(sizeof(pthread_spinlock_t) * ictx->node_mtx_num);
	if(ictx->node_mtx == NULL)
	{
		r = -ENOMEM;
		goto FINALLY;
	}
	for(i = 0; i != ictx->node_mtx_num; ++i)
	{
		pthread_spin_init(&ictx->node_mtx[i], 0);
	}


FINALLY:

	return r;
}

void s5_volumectx_node_mtx_release(struct s5_volume_ctx* ictx)	
{
	uint64 i = 0;

	if(ictx->node_mtx)
	{
		for(i = 0; i != ictx->node_mtx_num; ++i)
		{
			pthread_spin_destroy(&ictx->node_mtx[i]);
		}
		free((void*)(ictx->node_mtx));
		ictx->node_mtx = NULL;
	}
	pthread_spin_destroy(&ictx->io_num_lock);

	return;
}

int32 s5_volumectx_node_cache_init(struct s5_volume_ctx* ictx)
{
	int r = EOK;
	int i = 0;
	int j = 0; 
	int k = 0;	

	ictx->node_cache = (struct s5_unitnode*)malloc(sizeof(struct s5_unitnode) * (size_t)(ictx->session_conf.s5_io_depth + 1));
	if(ictx->node_cache == NULL)
	{
		r = -ENOMEM;
		goto FINALLY;
	}
	memset(ictx->node_cache, 0, sizeof(struct s5_unitnode) * (size_t)(ictx->session_conf.s5_io_depth + 1));

	for(i = 0; i < ictx->session_conf.s5_io_depth + 1; ++i)
	{
		for(k = 0; k < MAX_REPLICA_NUM; k++)
		{
			ictx->node_cache[i].msg[k] = s5msg_create(0);

			if(ictx->node_cache[i].msg[k] == NULL)
			{
				r = -ENOMEM;
				goto RELEASE_CACHE;
			}
		}
	}

	goto FINALLY;

RELEASE_CACHE:

	if(ictx->node_cache)
	{
		for(j = 0; j < i; ++j)
		{
			for(k = 0; k < MAX_REPLICA_NUM; k++)
			{
				free(ictx->node_cache[j].msg[k]);
			}
		}
		free(ictx->node_cache);
		ictx->node_cache = NULL;
	}

FINALLY:

	return r;
}

void s5_volumectx_node_cache_release(struct s5_volume_ctx* ictx)	
{
	int i = 0;
    int k = 0;
	if(ictx->node_cache)
	{
		for(i = 0; i < ictx->session_conf.s5_io_depth + 1; ++i)
		{   
            for(k = 0; k < MAX_REPLICA_NUM; k++)
            {
			    free(ictx->node_cache[i].msg[k]);
            }
		}
		free(ictx->node_cache);
		ictx->node_cache = NULL;
	}

	return;
}

int32 s5_volumectx_open_volume_to_conductor(struct s5_volume_ctx* ictx, int index)
{
	int r = EOK;
	s5_message_t *msg_reply = NULL;
	s5_message_t *msg = NULL;
	
	//open volume by sending a message to conductor
	if (ictx->conductor_clt == NULL)
	{
		ictx->conductor_clt = s5socket_create(SOCKET_TYPE_CLT, 0, NULL);
		if(ictx->conductor_clt == NULL)
		{
			S5LOG_AND_SET_ERROR("Failed to create socket to connect conductor.");
			r = -ENOMEM;
			goto FINALLY;
		}
	}

	s5conductor_entry_t* conductor_0_entry = s5ctx_get_conductor_entry(ictx->s5_context, 0); 
	s5conductor_entry_t* conductor_1_entry = s5ctx_get_conductor_entry(ictx->s5_context, 1);

	r = s5socket_connect(ictx->conductor_clt, 
						 conductor_0_entry ? conductor_0_entry->front_ip : NULL,
						 conductor_1_entry ? conductor_1_entry->front_ip : NULL,
						 (uint16_t)(conductor_0_entry ? conductor_0_entry->front_port : 0),
						 (uint16_t)(conductor_1_entry ? conductor_1_entry->front_port : 0),	
						 RCV_TYPE_MANUAL, CONNECT_TYPE_TEMPORARY);
	if (r != EOK)
	{
		S5LOG_AND_SET_ERROR("Failed to connect any conductor");
		goto RELEASE_SOCKET;
	}

	msg = s5msg_create(LBA_LENGTH);
	if(msg == NULL)
	{
		S5LOG_AND_SET_ERROR("Failed to create request message, no memory left.");
		r = -ENOMEM;
		goto RELEASE_SOCKET;
	}
	
	msg->head.magic_num = S5MESSAGE_MAGIC;
	msg->head.msg_type = MSG_TYPE_OPENIMAGE;
	msg->head.listen_port = (0x0000);
	msg->head.data_len = LBA_LENGTH;
	memset(msg->data, 0, (size_t)msg->head.data_len);

	s5_cltreq_volume_open_param_t open_param;
	memset(&open_param, 0, sizeof(s5_cltreq_volume_open_param_t));

	int replica_index = 0;
	if (index != -1)
	{
		open_param.nic_ip_blacklist_len = ictx->nic_ip_blacklist_len[index];
		memcpy(open_param.nic_ip_blacklist, ictx->nic_ip_blacklist[index], IPV4_ADDR_LEN * MAX_NIC_IP_BLACKLIST_LEN);
		open_param.replica_ctx_id[0] = ictx->replica_ctx_id[index];
		for(replica_index = 1; replica_index < MAX_REPLICA_NUM; replica_index++)
		{
			open_param.replica_ctx_id[replica_index] = -1;
		}
		
		open_param.replica_id = ictx->replica_id[index]; //open only this replica
	}
	else
	{
		for(replica_index = 0; replica_index < MAX_REPLICA_NUM; replica_index++)
		{
			open_param.replica_ctx_id[replica_index] = -1;
		}
		open_param.replica_id = ALL_REPLICA;
	}
	
	memcpy(open_param.volume_name, ictx->volume_name, MAX_NAME_LEN);
	memcpy(open_param.snap_name, ictx->snap_name, MAX_NAME_LEN);
	memcpy(open_param.tenant_name, ictx->tenant_name, MAX_NAME_LEN);
	memcpy(msg->data, &open_param, sizeof(s5_cltreq_volume_open_param_t));

	S5LOG_DEBUG("Open volume:%s, snap:%s, msg:",ictx->volume_name, ictx->snap_name);
	DUMP_MSG_HEAD(msg->head);
	
	S5LOG_TRACE("Open_volume:: send_msg_wait_reply before...\n");
	msg_reply = s5socket_send_msg_wait_reply(ictx->conductor_clt, msg);
	S5LOG_TRACE("Open_volume:: send_msg_wait_reply after reply:%p...\n", msg_reply);
	
	if(msg_reply)
	{
		if (msg_reply->head.status == MSG_STATUS_OK)
		{
			r = parse_open_volume_reply(ictx, msg_reply, index);
			if (r != EOK)
			{
				s5_clt_reply_t* clt_meta_rpl = (s5_clt_reply_t*)msg_reply->data;
				S5LOG_AND_SET_ERROR("Failed to open volume, as %s.", (char*)(clt_meta_rpl->reply_info.error_info));
				goto RELEASE_MSG;
			}
			
			if(msg_reply->head.data_len <= 0)
			{
				S5LOG_ERROR("Open_volume:: reply-msg data_len:%d err.\n", msg_reply->head.data_len);
				S5ASSERT(0);
				r = -3;
				goto RELEASE_MSG;
			}
		}
		else
		{
			r = -msg_reply->head.status;
			goto RELEASE_MSG;
		}
	}
	else
	{
		S5LOG_AND_SET_ERROR("Failed to send volume open request to conductor.");
		r = -ECOMM;
		goto RELEASE_MSG;
	}

RELEASE_MSG:

	s5msg_release_all(&msg_reply);
	s5msg_release_all(&msg);


RELEASE_SOCKET:

	if(ictx->conductor_clt != NULL)
	{
		s5socket_release(&ictx->conductor_clt, SOCKET_TYPE_CLT);
	}


FINALLY:

	return r;
}

int32 s5_volumectx_close_volume_to_conductor(struct s5_volume_ctx* ictx)
{
	int32 r = EOK;
	s5_message_t *msg_reply = NULL;
	s5_message_t *msg = NULL;

	//close volume by sending a message to conductor 
	if (ictx->conductor_clt == NULL)
	{
		ictx->conductor_clt = s5socket_create(SOCKET_TYPE_CLT, 0, NULL);
		if(ictx->conductor_clt == NULL)
		{
			r = - ENOMEM;
			goto FINALLY;
		}
	}

	s5conductor_entry_t* conductor_0_entry = s5ctx_get_conductor_entry(ictx->s5_context, 0);  
    s5conductor_entry_t* conductor_1_entry = s5ctx_get_conductor_entry(ictx->s5_context, 1);

    r = s5socket_connect(ictx->conductor_clt, 
                         conductor_0_entry ? conductor_0_entry->front_ip : NULL,
                         conductor_1_entry ? conductor_1_entry->front_ip : NULL,
                         (uint16_t)(conductor_0_entry ? conductor_0_entry->front_port : 0),
                         (uint16_t)(conductor_1_entry ? conductor_1_entry->front_port : 0),  
                         RCV_TYPE_MANUAL, CONNECT_TYPE_TEMPORARY);

	if(r != EOK)
	{
		goto RELEASE_SOCKET;
	}

	msg = s5msg_create(LBA_LENGTH);
	if(msg == NULL)
	{
		r = -ENOMEM;
		goto RELEASE_SOCKET;
	}
	msg->head.magic_num = S5MESSAGE_MAGIC;
	msg->head.msg_type = MSG_TYPE_CLOSEIMAGE;
	msg->head.data_len = LBA_LENGTH;
	msg->head.volume_id = ictx->volume_id;
	msg->head.snap_seq = ictx->snap_seq;
	if(strlen(ictx->snap_name)!=0){
		msg->head.is_head = 0 ; //snapshot
	}else{
		msg->head.is_head = 1; //head
	}

	s5_cltreq_volume_close_param_t close_param;
	memset(&close_param, 0, sizeof(s5_cltreq_volume_close_param_t));
	memcpy(close_param.replica_ctx_id, ictx->replica_ctx_id, sizeof(close_param.replica_ctx_id));
	memcpy(close_param.tenant_name, ictx->tenant_name, MAX_NAME_LEN);
	memcpy(msg->data, &close_param, sizeof(s5_cltreq_volume_close_param_t));

	S5LOG_TRACE("Close_volume:: send_msg_wait_reply before...\n");
	msg_reply = s5socket_send_msg_wait_reply(ictx->conductor_clt, msg);
	S5LOG_TRACE("Close_volume:: send_msg_wait_reply after reply:%p...\n", msg_reply);
	if(msg_reply)
	{
		S5LOG_TRACE("Close_volume:: reply-msg type:%d nlba:%d status:%d\n",
				 msg_reply->head.msg_type, msg_reply->head.nlba, msg_reply->head.status);
		r = EOK;
	}
	else
	{
		S5LOG_WARN("Close_volume:: reply-msg err msg_reply:%p\n", msg_reply);
		r = -ENOMEM;
		goto RELEASE_MSG;
	}


RELEASE_MSG:

	s5msg_release_all(&msg_reply);
	s5msg_release_all(&msg);

RELEASE_SOCKET:

	if(ictx->conductor_clt != NULL)
	{
		s5socket_release(&ictx->conductor_clt, SOCKET_TYPE_CLT);
	}


FINALLY:

	return r;
}

static void send_stat_info_to_conductor(s5_volume_ctx_t* ictx)
{
	s5_context_t* context = ictx->s5_context;

	int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr={0};

    addr.sin_family = AF_INET;       // Internet address

	int count = context->conductor_list.count;
	int i = 0;

	BOOL send_ok = FALSE;
	for(; i < count; i++) 
	{
		s5conductor_entry_t* entry = s5ctx_get_conductor_entry(context, i);

		addr.sin_addr.s_addr = inet_addr(entry->front_ip);
		addr.sin_port = htons((uint16_t)entry->spy_port);     // Assign port in network byte order
		socklen_t addr_len = sizeof(addr);

		char stat_info[200];

		int str_len = snprintf(stat_info, sizeof(stat_info), 
							   "write runtime_info vol_ctx_id %d average_latency %f band_width %u iops %f", 
							   ictx->replica_ctx_id[0], 
							   ictx->latency_per_second, 
							   ictx->band_width_per_second, 
							   ictx->iops_interval);		

		stat_info[str_len] = '\0';

		ssize_t rc = sendto(socket_fd, stat_info, (unsigned)str_len, 0, (struct sockaddr*)&addr, addr_len);
		if(rc != -1)
		{
			send_ok = TRUE;
		}
	}	

	if(send_ok == FALSE)
	{	
		S5LOG_ERROR("Failed to send statistic info to any conductor\n");
	}

	close(socket_fd);

	
}

static void* stat_running_info(void *arg)
{
    s5_volume_ctx_t *ictx = (s5_volume_ctx_t*)arg;
    while(TRUE)
    {    
        sleep(STAT_IO_TIME_INTERVAL);

        ictx->iops_interval = ((float)ictx->callback_deq_count_interval) / ((float)STAT_IO_TIME_INTERVAL);
        ictx->band_width_per_second = ictx->band_width_interval / STAT_IO_TIME_INTERVAL;

        if(ictx->callback_deq_count_interval > 0) 
        {    
            ictx->latency_per_second = (float)(ictx->latency_interval / ictx->callback_deq_count_interval);
        }    

        s5_atomic_set(&(ictx->callback_deq_count_interval), 0);
        s5_atomic_set(&(ictx->band_width_interval), 0);
        s5_atomic_set(&(ictx->latency_interval), 0);

		send_stat_info_to_conductor(ictx);
    }    

    return NULL;
}

static void *callback_thread_proc(void* arg)
{
	struct s5_volume_ctx *s5ctx = (struct s5_volume_ctx *)arg;
	struct sigaction sa={.sa_handler=SIG_IGN};
	sigaction(SIGUSR1, &sa, NULL);
	
	int i=0;
	int iExitCnt=0;	
	int tidmask = 0xff;

	s5ctx->callback_exit_flag = FALSE;

	while(!s5ctx->callback_exit_flag)
	{
		s5io_queue_item_t* s5io = NULL;

		for(i = 0; i < s5ctx->replica_num; i++)
		{
			while(lfds611_queue_dequeue(s5ctx->session[i].callback_queue, (void**)&s5io))
			{
				if((s5ctx->replica_num == 1) || (s5io->msg->head.msg_type == MSG_TYPE_READ_REPLY))
				{
					s5io->callback(s5io->cbk_arg);
				
					pthread_spin_lock(&s5ctx->io_num_lock);
					--s5ctx->io_num;
    				pthread_spin_unlock(&s5ctx->io_num_lock);


					free_s5io_queue_item(s5io);
					s5_atomic_add(&s5ctx ->callback_deq_count, 1);
	          		s5_atomic_add(&s5ctx ->callback_deq_count_interval, 1);
				}
				else
				{
					if(++s5ctx->s5io_cb_array[s5io->msg->head.transaction_id & tidmask] == s5ctx->replica_num)
					{
						s5io->callback(s5io->cbk_arg);
			
					    pthread_spin_lock(&s5ctx->io_num_lock);
   						--s5ctx->io_num;
    					pthread_spin_unlock(&s5ctx->io_num_lock);

						s5_atomic_add(&s5ctx ->callback_deq_count, 1);
	          			s5_atomic_add(&s5ctx ->callback_deq_count_interval, 1);
						s5ctx->s5io_cb_array[s5io->msg->head.transaction_id & tidmask] = 0;
						free_s5io_queue_item(s5io);
					}
				}
			}

			if(s5ctx->session[i].exit_flag)
			{
				iExitCnt++;
			}
		}

		if(iExitCnt == s5ctx->replica_num)
		{
			return NULL;
		}
		
		int64 r;
 		read(s5ctx->callback_thread_eventfd, &r, sizeof(r));
	}
	return NULL;
}

int32 open_volume(struct s5_volume_ctx* ictx)
{
	int session_index = 0;

	int r = s5_volumectx_open_volume_to_conductor(ictx, -1);
	if (r)
	{
		goto CONDUCTOR_OPEN_FAIL;
	}

	// init session
	for(session_index = 0; session_index < ictx->replica_num; session_index++)
	{
		r = s5session_init(&ictx->session[session_index], ictx, &ictx->session_conf, session_index);
		if (r)
		{
			S5LOG_AND_SET_ERROR("Failed to init session with toe(ip: %s, port: %d).", ictx->nic_ip[session_index], ictx->nic_port[session_index]);
			goto SESSION_INIT_FAIL;
		}
	}

	size_t elem_cnt = (size_t)(ictx->session_conf.s5_io_depth+1) * sizeof(int);
	ictx->s5io_cb_array = (int *)malloc(elem_cnt);
	if(ictx->s5io_cb_array != NULL)
	{
		memset(ictx->s5io_cb_array, 0, elem_cnt);
	}
	else
	{
		goto SESSION_INIT_FAIL;
	}	
	
	ictx->callback_thread_eventfd = eventfd(0,0);

	r = pthread_create(&ictx->callback_thread_id, NULL, callback_thread_proc, ictx);
	if(r)
	{
		S5LOG_ERROR("Failed to call pthread_create. errno: %d %s.",  errno, strerror(errno));
		goto CALLBACK_FAIL;
	}

	char iops[STAT_VAR_LEN] = IO_STAT_IOPS;
	strcat(iops, ictx->_suffix_stat_var);

	char band_width[STAT_VAR_LEN] = IO_STAT_BANDWIDTH;
	strcat(band_width, ictx->_suffix_stat_var);

	char average_latency[STAT_VAR_LEN] = IO_STAT_AVERAGE_LATENCY;
	strcat(average_latency, ictx->_suffix_stat_var);

	spy_register_variable(iops, (void*)&ictx->iops_interval, vt_float, "IOPS during one interval");
	spy_register_variable(band_width, (void*)&ictx->band_width_per_second, vt_uint32, "Band width per second");
	spy_register_variable(average_latency, (void*)&ictx->latency_per_second, vt_float, "Average latency for each IO in one second");
	spy_register_variable("s_callback_deq", (void*)&ictx->callback_deq_count, vt_int32, "Total IO popped from callback queue");

	r = s5_volumectx_init(ictx);
	if (r)
	{
		S5LOG_AND_SET_ERROR("Init volume context failed.");
		goto CTX_INIT_FAIL; 
	}
	
	r = init_stat_thread(ictx);
    if (r)
    {    
        S5LOG_AND_SET_ERROR("Init runtime-info-stat thread failed, for detailed please refer to s5bd log.");
        goto FINALLY;
    } 

	return 0;

	int j = 0;

FINALLY:

CTX_INIT_FAIL:
	spy_unregister(iops);
	spy_unregister(band_width);
	spy_unregister(average_latency);
	spy_unregister("s_callback_deq");

	for(; j < session_index; j++)
	{
		s5session_destory(&ictx->session[j]);
	}
CALLBACK_FAIL:
	if(ictx->s5io_cb_array != NULL)
	{
		free(ictx->s5io_cb_array);
	}
SESSION_INIT_FAIL:
	s5_volumectx_close_volume_to_conductor(ictx);
CONDUCTOR_OPEN_FAIL:
	return r;
}

int32 close_volume(struct s5_volume_ctx* ictx)
{
	int32 r = EOK;

	pthread_spin_lock(&ictx->io_num_lock);
	if(ictx->io_num != 0)
	{
		pthread_spin_unlock(&ictx->io_num_lock);
		r = -EAGAIN;
		goto FINALLY;
	}
	pthread_spin_unlock(&ictx->io_num_lock);

	char iops[STAT_VAR_LEN] = IO_STAT_IOPS;
	strcat(iops, ictx->_suffix_stat_var);

	char band_width[STAT_VAR_LEN] = IO_STAT_BANDWIDTH;
	strcat(band_width, ictx->_suffix_stat_var);

	char average_latency[STAT_VAR_LEN] = IO_STAT_AVERAGE_LATENCY;
	strcat(average_latency, ictx->_suffix_stat_var);

	spy_unregister(iops);
	spy_unregister(band_width);
	spy_unregister(average_latency);
	spy_unregister("s_callback_deq");

	r = s5_volumectx_release(ictx);
	if(r != EOK)
	{
		S5LOG_AND_SET_ERROR("Close_volume::s5_volumectx_release errorno(%d)\n", r);
	}

	int index = 0;
	for(; index < ictx->replica_num; index++)
	{
		s5session_destory(&ictx->session[index]);
	}
	r = s5_volumectx_close_volume_to_conductor(ictx);
	if(r != EOK)
	{
		S5LOG_AND_SET_ERROR("Close_volume::s5_volumectx_close_volume_to_condcutor errorno(%d)\n", r);
	}
	
	r = exit_thread(ictx->stat_thread);
	if(r != EOK)
	{
		S5LOG_AND_SET_ERROR("Close_volume::exit_thread stat_thread errorno(%d)\n", r);
	}
	
	free(ictx->s5io_cb_array);	
	if(ictx->callback_thread_id)
	{
		ictx->callback_exit_flag = TRUE;
		write(ictx->callback_thread_eventfd, &event_delta, sizeof(event_delta));
		pthread_kill(ictx->callback_thread_id, SIGUSR1);
		pthread_join(ictx->callback_thread_id, NULL);
	}
	if(ictx ->callback_thread_eventfd >= 0)
	{
		close(ictx ->callback_thread_eventfd);
	}

FINALLY:

	return r;
}

int32 init_stat_thread(struct s5_volume_ctx* ictx)
{
	int32 r = pthread_create(&ictx->stat_thread, NULL, stat_running_info, ictx);
	if (r != 0)
	{
		S5LOG_ERROR("Stat thread creation failed : %d.", r);
	}

	return r;
}

int32 parse_open_volume_reply(struct s5_volume_ctx* ictx, s5_message_t *msg_reply, int replica_index)
{
	if (MSG_STATUS_OPENIMAGE_ERR == msg_reply->head.status)
	{
		return -MSG_STATUS_OPENIMAGE_ERR;
	}

	ictx->iops_per_GB = msg_reply->head.iops_density;

	if(msg_reply->head.data_len > 0)
	{
		s5_clt_reply_t* open_reply = (s5_clt_reply_t*)msg_reply->data;
		S5ASSERT(open_reply);
	
		int32 rc = open_reply->result;
		if (rc != 0)
		{
			return rc;
		}

		ictx->volume_id = open_reply->reply_info.open_rpl_data->meta_data.volume_id;
		ictx->replica_num = open_reply->reply_info.open_rpl_data->meta_data.replica_count;
		
		if(replica_index != -1)
		{//open certain replica only
			ictx->replica_id[replica_index] = open_reply->reply_info.open_rpl_data->meta_data.replica_id[0];
			ictx->snap_seq = open_reply->reply_info.open_rpl_data->meta_data.snap_seq;
			ictx->volume_size = open_reply->reply_info.open_rpl_data->meta_data.volume_size;

			memcpy(ictx->nic_ip[replica_index], open_reply->reply_info.open_rpl_data->nic_ip[0], IPV4_ADDR_LEN);
			ictx->nic_port[replica_index] = open_reply->reply_info.open_rpl_data->nic_port[0];
			ictx->replica_ctx_id[replica_index] = open_reply->reply_info.open_rpl_data->replica_ctx_id[0];
			S5LOG_DEBUG("Open_volume:%s, replica_id:%lu, volume_size:%lu, nic_ip:%s, nic_port:%d, replica_ctx_id:%d",
				ictx->volume_name, ictx->replica_id[replica_index], ictx->volume_size, ictx->nic_ip[replica_index],
				ictx->nic_port[replica_index], ictx->replica_ctx_id[replica_index]);
		}
		else
		{//all replicas are openned
			memcpy(ictx->replica_id, open_reply->reply_info.open_rpl_data->meta_data.replica_id, sizeof(ictx->replica_id));
			ictx->snap_seq = open_reply->reply_info.open_rpl_data->meta_data.snap_seq;
			ictx->volume_size = open_reply->reply_info.open_rpl_data->meta_data.volume_size;

			memcpy(ictx->nic_ip, open_reply->reply_info.open_rpl_data->nic_ip, IPV4_ADDR_LEN * MAX_REPLICA_NUM);
			memcpy(ictx->nic_port, open_reply->reply_info.open_rpl_data->nic_port, sizeof(ictx->nic_port));
			memcpy(ictx->replica_ctx_id, open_reply->reply_info.open_rpl_data->replica_ctx_id, sizeof(ictx->replica_ctx_id));
			
			int skip = 0;
			int ok_rep = 0;
			int i;
			for (i = 0; i < MAX_REPLICA_NUM; i++)
			{
				if (ictx->replica_ctx_id[i] == -1)
				{
					skip++;
					continue;
				}
				if (skip != 0)
				{
					ictx->replica_id[ok_rep] = ictx->replica_id[i];
					memcpy(ictx->nic_ip[ok_rep], ictx->nic_ip[i], sizeof(ictx->nic_ip[0]));
					ictx->nic_port[ok_rep] = ictx->nic_port[i];
					ictx->replica_ctx_id[ok_rep] = ictx->replica_ctx_id[i];
				}
				ok_rep++;
			}
			int invalid_index = ictx->replica_num = ok_rep;
			for (; invalid_index < MAX_REPLICA_NUM; invalid_index++)
			{
				ictx->replica_ctx_id[invalid_index] = -1;
			}
		}
	}

	char snap_id_str[32], volume_id_str[64];
	sprintf(volume_id_str, "_%lu", ictx->volume_id);
	sprintf(snap_id_str, "_%d", ictx->snap_seq);
	memset(ictx->_suffix_stat_var, 0, SUFFIX_STAT_VAR_LEN);
	strcat(ictx->_suffix_stat_var, volume_id_str);
	strcat(ictx->_suffix_stat_var, snap_id_str);
	
	return EOK;
}

void recv_session_aio_reply(void *param)
{
	struct s5_unitnode *unode = (struct s5_unitnode*)param;
	s5_volume_ctx_t *ictx = unode->ictx;
	s5_message_t *msg = unode->msg[0];

	int64 bid = msg->head.slba / (S5_OBJ_LEN / LBA_LENGTH);

	unsigned int unode_band_width = 0;

	s5_aiocompletion_t *comp = NULL;

	S5ASSERT(NULL != msg);
	S5ASSERT(msg->head.slba >= unode->ofs / LBA_LENGTH);
	S5ASSERT(msg->head.slba + msg->head.nlba <= (unode->ofs + unode->len) / LBA_LENGTH);

	unode_band_width = unode->nlba * LBA_LENGTH ;
	s5_atomic_add(&(ictx->band_width_interval), unode_band_width);

	struct timeval task_end;
	gettimeofday(&task_end, NULL);
	long time_use = (task_end.tv_sec - unode->task_start.tv_sec) * 1000000 + (task_end.tv_usec - unode->task_start.tv_usec);
	s5_atomic_add(&(ictx->latency_interval), time_use);

	//lock node_mtx lock when accessing blocknode
	pthread_spin_lock(&ictx->node_mtx[bid % ictx->node_mtx_num]);

	unode->comp->filled += (uint32)msg->head.nlba;
	int status = msg->head.status & 0xff;
	if (status)
	{
		S5LOG_ERROR("S5 error:0x%x", msg->head.status);
		unode->comp->status = status; //save only last IO error
	}
	if(unode->comp->filled == unode->comp->nlba)
	{
		comp = unode->comp;
	}

	//update blocknode
	//1. reset bit array
	//2. minus running num
	//3. dispatch task
	//4. minus s5bd io depth
	struct s5_blocknode* bnode = ictx->slotlist[bid];
	S5ASSERT(bnode != NULL);

	bitarray_reset(bnode->barr, (int)(msg->head.slba % SLOT_SIZE), msg->head.nlba);

	--bnode->running_num;

	free_id(ictx->idg, msg->head.transaction_id);

	if(bnode->running_num == 0)
	{
		dispatch_task(bnode);	
	}

	if(bnode->flag == NODE_IDLE)
	{
		s5_blocknode_release(bnode);
		free(bnode);
		ictx->slotlist[bid] = NULL;
	}

	pthread_spin_unlock(&ictx->node_mtx[bid % ictx->node_mtx_num]);


	if(comp)
	{
		s5_aiocompletion_complete(comp);
	}

	return ;
}

//give the 4M block number according to offset and length
size_t get_io_num(uint64_t off, size_t len)
{
	size_t num = 0;
	size_t left = off % S5_OBJ_LEN;
	int64 length = (int64)len;
	if(left)
	{
		num = 1;
		length = (int64)(len + left - S5_OBJ_LEN);
	}

	if(length > 0)
	{
		num += (size_t)(length / S5_OBJ_LEN);
		if(length % S5_OBJ_LEN)
			num += 1;
	}

	return num;
}

void build_msg(struct s5_unitnode* unode)
{
	s5_volume_ctx_t *ictx = unode->ictx;

	S5ASSERT(unode != NULL);

	S5ASSERT(unode->msg != NULL);

	int index = 0;
	for(; index < ictx->replica_num; index++)
	{
		if(unode->flag == NODE_AIO_READ)
		{
			unode->msg[index]->head.msg_type = MSG_TYPE_READ;
			unode->msg[index]->data = unode->readbuf;

			if (unode->nlba == 1)
				unode->msg[index]->head.read_unit = 0;
			else if (unode->nlba == 2)
				unode->msg[index]->head.read_unit = 1;
			else
				unode->msg[index]->head.read_unit = 2;

			//S5LOG_INFO("read_unit = %d.", unode->msg[index]->head.read_unit);
		}
		else if(unode->flag == NODE_AIO_WRITE)
		{
			unode->msg[index]->head.msg_type = MSG_TYPE_WRITE;
			unode->msg[index]->head.data_len = (int32)unode->len;
			unode->msg[index]->data = (void*)unode->writedata;
		}
		else
		{
			S5ASSERT(0);
		}

		unode->msg[index]->head.magic_num = S5MESSAGE_MAGIC;
		unode->msg[index]->head.user_id = ictx->user_id;
		unode->msg[index]->head.volume_id = ictx->replica_id[index];
		unode->msg[index]->head.transaction_id = unode->task_id;
		unode->msg[index]->head.snap_seq = ictx->snap_seq;
		unode->msg[index]->head.slba = (int64)(unode->ofs / LBA_LENGTH);
		unode->msg[index]->head.nlba = (int32)(unode->len / LBA_LENGTH);
		unode->msg[index]->head.iops_density = ictx->iops_per_GB;

		if (strlen(ictx->snap_name) == 0)
		{
			unode->msg[index]->head.is_head = TRUE;
		}
		else
		{
			unode->msg[index]->head.is_head = FALSE;
		}
	}
}

void send_unit_node(struct s5_unitnode* unode)
{
	int32 r = 0;
	s5_volume_ctx_t *ictx = unode->ictx;

	if(MSG_TYPE_READ == unode->msg[0]->head.msg_type)
	{
		if(ictx->replica_num > 1)
		{
			int iStartIdx=ictx->read_session_idx;
	
			while(ictx->session[ictx->read_session_idx].exit_flag)
			{
				ictx->read_session_idx++;
				ictx->read_session_idx = ictx->read_session_idx % ictx->replica_num;
			
				if(ictx->read_session_idx == iStartIdx)
				{
					S5LOG_ERROR("All sessions exit, failed to read");
					return;
				}
			}

			r = s5session_aio_read(&(ictx->session[ictx->read_session_idx]), 
								 unode->msg[ictx->read_session_idx], 
								 recv_session_aio_reply, unode);
			S5ASSERT(0 == r);
			ictx->read_session_idx++;
			ictx->read_session_idx = ictx->read_session_idx % ictx->replica_num;
		}
		else
		{
			r = s5session_aio_read(&(ictx->session[0]),
                                  unode->msg[0],
                                  recv_session_aio_reply, unode);
			S5ASSERT(0 == r);
		}
	}
	else if(MSG_TYPE_WRITE == unode->msg[0]->head.msg_type)
	{
		int i = 0;

		for(i=0; i < ictx->replica_num; i++)
		{
			if(ictx->session[i].exit_flag)
			{
				S5LOG_ERROR("All sessions exit, failed to write");
				return;	
			}
		}

		for(i=0; i < ictx->replica_num; i++)
		{
			r = s5session_aio_write(&(ictx->session[i]), unode->msg[i], recv_session_aio_reply, unode);
			S5ASSERT(0 == r);
		}
	}
	else
	{
		S5ASSERT(0);
	}

//	LOG_DEBUG("send_unit_node:: socket_send_msg after rc:%d ...\n", r);

	return;
}

int32 dispatch_task(struct s5_blocknode *bnode)
{
	S5ASSERT(NULL != bnode);

	if(s5_unitnode_queue_empty(&bnode->readyqueue))
	{
		S5ASSERT(bnode->flag == NODE_UNBLOCKED);
		bnode->flag = NODE_IDLE;
		return NODE_IDLE;
	}
	else
	{
		S5ASSERT(bnode->flag == NODE_BLOCKED);
		struct s5_unitnode *unode = s5_unitnode_queue_head(&bnode->readyqueue);
		
		while(unode)
		{
			if(bitarray_set(bnode->barr, 
						 	(int)(unode->msg[0]->head.slba % SLOT_SIZE), 
							unode->msg[0]->head.nlba))
			{
				s5_unitnode_queue_dequeue(&bnode->readyqueue);
				++bnode->running_num;
				bnode->flag = NODE_UNBLOCKED;
				send_unit_node(unode);
			}
			else
			{
				bnode->flag = NODE_BLOCKED;
				break;
			}
			unode = s5_unitnode_queue_head(&bnode->readyqueue);
		}

	}

	return bnode->flag;
}

void process_task(struct s5_unitnode *unode)
{
	BOOL is_send = FALSE;
	BOOL is_unitnode_empty = FALSE;
	BOOL is_bitarray_set = FALSE;
	struct s5_volume_ctx *ictx = unode->ictx;
	uint64 bid = unode->ofs / S5_OBJ_LEN;

	gettimeofday(&(unode->task_start), NULL);

	//lock node_mtx lock when accessing blocknode
	pthread_spin_lock(&ictx->node_mtx[bid % ictx->node_mtx_num]);
	if(NULL == ictx->slotlist[bid])
	{
		ictx->slotlist[bid] = (struct s5_blocknode*)malloc(sizeof(struct s5_blocknode));
		s5_blocknode_init(ictx->slotlist[bid]);
	}

	struct s5_blocknode* bnode = ictx->slotlist[bid];

	if(bnode->flag == NODE_UNBLOCKED)
	{
		if(bitarray_set(bnode->barr, 
						(int)(unode->msg[0]->head.slba % SLOT_SIZE), 
						unode->msg[0]->head.nlba))
		{
			is_unitnode_empty = s5_unitnode_queue_empty(&bnode->readyqueue);
			S5ASSERT(is_unitnode_empty);
			++bnode->running_num;
			is_send = TRUE;
		}
		else
		{
			bnode->flag = NODE_BLOCKED;
			s5_unitnode_queue_enqueue(&bnode->readyqueue, unode);
		}
	}
	else if(bnode->flag == NODE_BLOCKED)
	{
		s5_unitnode_queue_enqueue(&bnode->readyqueue, unode);
	}
	else//bnode->flag == NODE_IDLE
	{
		is_bitarray_set = bitarray_set(bnode->barr, 
									  (int)(unode->msg[0]->head.slba % SLOT_SIZE), 
									  unode->msg[0]->head.nlba);
		S5ASSERT(is_bitarray_set);
		bnode->flag = NODE_UNBLOCKED;
		++bnode->running_num;
		is_send = TRUE;
	}
	pthread_spin_unlock(&ictx->node_mtx[bid % ictx->node_mtx_num]);


	if(is_send)
	{
		send_unit_node(unode);
	}

	return;
}

void _aio_read(struct s5_volume_ctx *ictx, uint64_t off, size_t len, char *buf, struct s5_aiocompletion *comp)
{
	int id = INVALID_ID;

	uint64_t slba = off / LBA_LENGTH;
	uint32 nlba = (uint32)(len / LBA_LENGTH);
	size_t block_slots = CACHE_BLOCK_SIZE;
	const uint64_t start = slba;
	const uint64_t end = start + (uint64_t)nlba;

	size_t block_off = slba % block_slots;

	comp->nlba = nlba;
	comp->filled = 0;

	do
	{
		uint32 tmp_nlba = (uint32)(block_slots - block_off);
		if(tmp_nlba > nlba)
		{
			tmp_nlba = nlba;
		}

		id = alloc_id(ictx->idg);
		S5ASSERT(id >= 0 && id <= ictx->session_conf.s5_io_depth);
		struct s5_unitnode *unode = &ictx->node_cache[id];
		s5_unitnode_reset(unode);

		unode->task_id = id;
		unode->nlba = tmp_nlba;
		unode->len = tmp_nlba * LBA_LENGTH;
		unode->ofs = slba * LBA_LENGTH;
		unode->readbuf = buf + (slba - start) * LBA_LENGTH;
		unode->comp = comp;
		unode->ictx = ictx;
		unode->flag = NODE_AIO_READ;

		build_msg(unode);

		process_task(unode);

		slba += tmp_nlba;
		nlba -= tmp_nlba;
		block_off = slba % block_slots;
	}
	while(slba < end);
}

void _aio_write(struct s5_volume_ctx *ictx, uint64_t off, size_t len, const char *buf, struct s5_aiocompletion *comp)
{
	int id = INVALID_ID;

	uint64_t slba = off / LBA_LENGTH;
	uint32 nlba = (uint32)(len / LBA_LENGTH);
	size_t block_slots = CACHE_BLOCK_SIZE;
	const uint64_t start = slba;
	const uint64_t end = start + nlba;

	size_t block_off = slba % block_slots;

	comp->nlba = nlba;
	comp->filled = 0;

	do
	{
		uint32 tmp_nlba = (uint32)(block_slots - block_off);
		if(tmp_nlba > nlba)
		{
			tmp_nlba = nlba;
		}

		id = alloc_id(ictx->idg);
		S5ASSERT(id >= 0 && id <= ictx->session_conf.s5_io_depth);
		struct s5_unitnode *unode = &ictx->node_cache[id];
		s5_unitnode_reset(unode);

		unode->task_id = id;
		unode->nlba = tmp_nlba;
		unode->len = tmp_nlba * LBA_LENGTH;
		unode->ofs = slba * LBA_LENGTH;
		unode->writedata = buf + (slba - start) * LBA_LENGTH;
		unode->comp = comp;
		unode->ictx = ictx;
		unode->flag = NODE_AIO_WRITE;

		build_msg(unode);

		process_task(unode);

		slba += tmp_nlba;
		nlba -= tmp_nlba;
		block_off = slba % block_slots;

	}
	while(slba < end);
}

ssize_t _sio_read(struct s5_volume_ctx *ictx, uint64_t ofs, size_t len, char *buf)
{
	ssize_t ret = 0;

	ret = update_io_num_and_len(ictx, ofs, &len);
	if(ret != 0) 
	{
		return ret;
	}
	
	s5_aiocompletion_t *comp = s5_aio_create_completion(NULL, NULL, TRUE);
	_aio_read(ictx, ofs, len, buf, comp);

	s5_aiocompletion_wait_for_complete(comp);

	ret = s5_aiocompletion_get_return_value(comp);
	s5_aio_release_completion(comp);

	return ret;
}

ssize_t _sio_write(struct s5_volume_ctx *ictx, uint64_t ofs, size_t len, const char *buf)
{
	ssize_t ret = 0;

	ret = update_io_num_and_len(ictx, ofs, &len);
	if(ret != 0) 
	{
		return ret;
	}
	
	s5_aiocompletion_t *comp = s5_aio_create_completion(NULL, NULL, TRUE);

	_aio_write(ictx, ofs, len, buf, comp);

	s5_aiocompletion_wait_for_complete(comp);

	ret = s5_aiocompletion_get_return_value(comp);
	s5_aio_release_completion(comp);

	return ret;
}

int update_io_num_and_len(struct s5_volume_ctx *ictx, uint64_t off, size_t *len)
{
	if(off % LBA_LENGTH != 0 || *len % LBA_LENGTH != 0 || off >= ictx->volume_size)
	{
		return -EINVAL;
	}

	if(off + *len > ictx->volume_size)
	{
		S5LOG_WARN("**************** I/O exceeds the volume size ************");
		*len = ictx->volume_size - off;
	}

	size_t io_num = get_io_num(off, *len);
	pthread_spin_lock(&ictx->io_num_lock);
	size_t num = io_num + ictx->io_num;
	if(num > ictx->session_conf.s5_io_depth)
	{
		pthread_spin_unlock(&ictx->io_num_lock);
		return -EAGAIN;
	}

	ictx->io_num = num;
	pthread_spin_unlock(&ictx->io_num_lock);
	return 0;
}

