/*
 * =====================================================================================
 *
 *       Filename:  libs5mq.c
 *
 *    Description:
 *
 *        Version:  0.1
 *        Created:  09/22/2015 17:18:52
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  FanXiaoGuang(xiaoguang.fan@netbric.com)
 *   Organization:  NETBRIC
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>   /*  For SYS_xxx definitions */

#include "pf_mq.h"
#include "pf_mq_common.h"
#include "pf_mq_pack_unpack.h"

#include "pf_list.h"
#include "pf_conf.h"
#include "pf_errno.h"
#include "parse_config.h"
#include "broker.h"

#define _GNU_SOURCE        /*  or _BSD_SOURCE or _SVID_SOURCE */

unsigned long int g_hb_lasttime = 1;
unsigned int g_time_interval = 5;
int g_is_running;
mq_clnt_ctx_t g_mq_clnt_ctx;

static pf_dlist_head_t sendlist;
static pf_dlist_head_t recvlist;

extern pf_dlist_head_t cndctlist;
extern pf_dlist_head_t workerlist;
static broker_t broker;

#define INSIDE_CTRL_ADDR         "inproc://s5mq_inproc"

typedef void *(*pthread_f)(void *);
static int init_ctx_from_cfg(const char *conf_file, s5mq_id_type_t id_type, mq_clnt_ctx_t *ctx);
static int init_cndct_ctx_from_cfg(const char *conf_file, mq_clnt_ctx_t *ctx);
static int init_worker_ctx_from_cfg(const char *conf_file, mq_clnt_ctx_t *ctx);

static void *thread_clnt_event(void *args);
static int send_msg_to_broker(void *sock);
static int create_worker_reg_msg(zmsg_t *zmsg, int reconect_flag);
static int create_cndct_reg_msg(zmsg_t *zmsg);
static int clnt_reg_to_broker(void *sock, s5mq_id_type_t id_type, int reconnect_flag);
zmsg_t * my_recv_timeout(void *sock, int sec,  char *info);
static int clnt_create_mq_msg(const pf_message_t *sendmsg,
                        const char *target_id,
                        int reply_flag,
                        mqmsg_t *mqmsg);
static int pack_req_mqmsg(mqmsg_t *msgmq,   zmsg_t *zmsg);
static int unpack_recv_mqmsg(zmsg_t *zmsg, mqmsg_t *clnt_msg);

static void clnt_send_hb_msg(void *sock);
static void create_heartbeat_msg(zmsg_t *msg);
static int check_broker_status(mq_clnt_ctx_t *ctx);
static void deal_srv_msg(zmsg_t *msg);
static int  add_msg_to_recvlist(mqmsg_t *mqmsg);
static void deal_recv_msg(zmsg_t *zmsg);
static void deal_hb_msg(zmsg_t *zmsg);
static void deal_reg_msg(zmsg_t *zmsg);
static int create_detach_thread(pthread_f func,void *args);
static void do_reconnect(mq_clnt_ctx_t *ctx);
static int do_connect_to_slave(mq_clnt_ctx_t *ctx);
static int do_connect_to_master(mq_clnt_ctx_t *ctx);
static void set_worker_notify_info(pf_worker_status_t status);
static void set_cndct_notify_info(pf_conductor_role_t role);
static int do_worker_register(void *sock, int reconnect_flag);
static int do_cndct_register(void *sock);

/*-------------------------function implementations------------------------*/

/**
 * destroy client communication context
 *
 * @param void
 * @retval 0 for sucess,otherwize return error code
 */
void mq_clnt_ctx_destroy(void)
{
    S5LOG_INFO("S5MQ user call mq_clnt_ctx_destroy, will exit.\n");
	if (g_mq_clnt_ctx.connect_status)
	{
		zmsg_t *unreg = zmsg_new();
		zmsg_addstr(unreg, "UNREGISTER");
		zmsg_send(&unreg, g_mq_clnt_ctx.sock_out_ctrlend);
		zmsg_t *retmsg = my_recv_timeout(g_mq_clnt_ctx.sock_out_ctrlend, MQ_DESTROY_WAIT_TIME, "Out control wait do UN_REGISTER operation");
		if (NULL != retmsg)
		{
			char *str = zmsg_popstr(retmsg);
			//printf("thread_clnt_event exit:%s.\n", str);
			S5LOG_INFO("Thread thread_clnt_event exit:%s.", str);
			free(str);
		}
		else
		{
			S5LOG_INFO("Main thread exit, (failed received event thread).");
		}
		sleep(1);
		/* let broker exit */
		g_is_running = STOPPED;

		zmsg_destroy(&retmsg);
		zsocket_disconnect(g_mq_clnt_ctx.sock_out_ctrlend, INSIDE_CTRL_ADDR);
	}
	zsocket_destroy(g_mq_clnt_ctx.sock_ctx, g_mq_clnt_ctx.sock_out_ctrlend);
    zctx_destroy(&g_mq_clnt_ctx.sock_ctx);

    S5LOG_INFO("xxxxxxxxxxxxxx S5MQ  MAIN EXITxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
}

void *thread_clnt_event(void *args)
{
    zmsg_t          *ctrlmsg = NULL;
    zmsg_t          *zmsg = NULL;
	mq_clnt_ctx_t   *ctx = NULL;
	int             rc = 0;
    char            *tmp_str = NULL;

    ENTER_FUNC;
    if (NULL == args)
    {
        //printf("Invalid parameter in thread_clnt_event.\n");
        S5LOG_ERROR("Invalid parameter in thread_clnt_event.");
        LEAVE_FUNC;
        return (void*)(-1);
    }
    ctx = (mq_clnt_ctx_t *)args;
	ctx->sock_in_ctrlend = zsocket_new(ctx->sock_ctx,  ZMQ_PAIR);
	assert(ctx->sock_in_ctrlend);

    rc = zsocket_bind(ctx->sock_in_ctrlend, INSIDE_CTRL_ADDR);
    if (OK != rc )
    {
        //printf("Failed to bind on frontend addr:[%s](rc=%d,errno:%d, reason:%s).\n",
        S5LOG_ERROR("Failed to bind on frontend addr:[%s](rc=%d,errno:%d, reason:%s).",
                INSIDE_CTRL_ADDR, rc,errno, zmq_strerror(errno));
        zsocket_destroy(ctx->sock_ctx, ctx->sock_in_ctrlend);
        LEAVE_FUNC;
        return (void*)-1;
    }

    ctrlmsg = my_recv_timeout(ctx->sock_in_ctrlend, MQ_REG_WAIT_TIME, "Inside control wait DO_REGISTER");
    if(NULL == ctrlmsg)
    {
        //printf("No register order, just quit.\n");
        S5LOG_ERROR("No register order from outside control socket, just quit.");
        goto out;
    }
    tmp_str = zmsg_popstr(ctrlmsg);
    assert(tmp_str);
    if (strcmp(tmp_str, "DO_REGISTER"))
    {
        //printf("Invalid order, just quit.\n");
        S5LOG_ERROR("Invalid order, just quit.");
        free(tmp_str);
        zmsg_destroy(&ctrlmsg);
        goto out;
    }
    free(tmp_str);
    zmsg_destroy(&ctrlmsg);



	const char* broker_ip = ctx->ip2_slave;
	while (g_is_running)
	{
		ctx->sock_dataend = zsocket_new(ctx->sock_ctx, ZMQ_DEALER);
		assert(ctx->sock_dataend);
		zsocket_set_identity(ctx->sock_dataend, g_mq_clnt_ctx.self_id);
		zsocket_set_linger(ctx->sock_dataend, 0);
		if (ID_TYPE_CNDCT == ctx->self_type)
		{
			strncpy(ctx->ip1_master, LOCAL_ADDR, IPV4_ADDR_LEN - 1);
			broker_ip = ctx->ip1_master;
			/* if typeof ID is cndct, connect to frontend */
			S5LOG_INFO("Connnect to ip1 frontend %s:%d.", ctx->ip1_master, ctx->port_frontend);
			rc = zsocket_connect(ctx->sock_dataend, "tcp://%s:%d", ctx->ip1_master, ctx->port_frontend);
			if (0 != rc)
			{
				S5LOG_ERROR("Failed to do zsocket_connect addr [tcp://%s:%d](errno:%d, reason:%s)).\n",
					ctx->ip1_master, ctx->port_frontend, errno, zmq_strerror(errno));
				goto out;
			}
		}
		else
		{
			broker_ip = broker_ip == ctx->ip2_slave ? ctx->ip1_master : ctx->ip2_slave;
			/* if typeof ID is cndct, connect to frontend */
			S5LOG_INFO("Connnect to ip1 backend  %s:%d.", broker_ip, ctx->port_backend);
			rc = zsocket_connect(ctx->sock_dataend, "tcp://%s:%d", broker_ip, ctx->port_backend);
			if (0 != rc)
			{
				S5LOG_ERROR("Failed to do zsocket_connect addr [tcp://%s:%d](errno:%d, reason:%s)).\n",
					broker_ip, ctx->port_backend, errno, zmq_strerror(errno));
				continue;
			}

		}

		//printf("begin send register to broker.\n");
		S5LOG_INFO("Begin to send register message to broker.");
		rc = clnt_reg_to_broker(ctx->sock_dataend, ctx->self_type, FALSE);
		if (OK != rc)
		{
			//printf("Failed to do register to broker.(errno:%d, reason:%s)).\n",
			S5LOG_ERROR("Failed to do register to broker.(errno:%d, reason:%s)).",
				errno, zmq_strerror(errno));
			continue;
		}
		zmsg_t *regmsg = zmsg_new();
		zmsg_addstr(regmsg, "REGISTER_OK");
		zmsg_send(&regmsg, ctx->sock_in_ctrlend);
		ctx->is_connect_master = TRUE;
		ctx->connect_status = TRUE;
		break;
	}


	zmq_pollitem_t items [] = {
		{ctx->sock_in_ctrlend,  0,  ZMQ_POLLIN,  0 },
		{ctx->sock_dataend,  0,  ZMQ_POLLIN,  0 },
	};
	while(g_is_running)
	{
        clnt_send_hb_msg(ctx->sock_dataend);
		rc = zmq_poll(items,  2,  10*ZMQ_POLL_MSEC); //poll every 10 mili seconds
		if(-1 == rc )
		{
			//printf("Failed to do zmq_poll(errno=%d, reason:%s).\n",
			S5LOG_ERROR("Failed to do zmq_poll(errno=%d, reason:%s).",
					errno, zmq_strerror(errno));
			break;
		}
		if (items[0].revents & ZMQ_POLLIN)
		{
			zmsg = zmsg_recv(ctx->sock_in_ctrlend);
            char *str = zmsg_popstr(zmsg);
            assert(str);
            if (!strcmp(str, "UNREGISTER"))
            {
                S5LOG_INFO("S5MQ received UNREGISTER message in control socket,   set STOPPED flag.");
                g_is_running = STOPPED;
            }
            free(str);
            zmsg_destroy(&zmsg);
		}
		if (items[1].revents & ZMQ_POLLIN)
		{
			zmsg = zmsg_recv(ctx->sock_dataend);
            //zmsg_print(msg);
			deal_recv_msg(zmsg);
		}
		send_msg_to_broker(ctx->sock_dataend);
        rc = check_broker_status(ctx);
        if ( OK != rc)
        {
            //printf("Check broker status time out.Connection failed. exit...\n");
            S5LOG_ERROR("Check broker status time out.Connection failed.");
            do_reconnect(ctx);
            S5LOG_DEBUG("After do reconnect.");
        }
	}
    zmsg_t *unreg = zmsg_new();
    assert(unreg);
    zmsg_addstr(unreg, "UNREGISTER_OK");
    //zmsg_print(unreg);
    zmsg_send(&unreg, ctx->sock_in_ctrlend);
    S5LOG_DEBUG("S5MQ inside control socket send unregister message to outside.");
out:
    /** 
     * If is worker add_buf maybe not the address first connect
     * because if  may doreconnect.
     */
    if(ID_TYPE_CNDCT !=  ctx->self_type)
    {
        if (TRUE == ctx->is_connect_master)
        {
            zsocket_disconnect(ctx->sock_dataend, ctx->ip1_master,  ctx->port_backend);
        }
        else
        {
            zsocket_disconnect(ctx->sock_dataend, ctx->ip1_master, ctx->ip2_slave,  ctx->port_backend);
        }
    }
    zsocket_disconnect(g_mq_clnt_ctx.sock_out_ctrlend, INSIDE_CTRL_ADDR);
	zsocket_destroy(ctx->sock_ctx, ctx->sock_dataend);
    zsocket_unbind(ctx->sock_in_ctrlend, INSIDE_CTRL_ADDR);
	zsocket_destroy(ctx->sock_ctx, ctx->sock_in_ctrlend);

    S5LOG_INFO("----------------------------------------------------------");
    S5LOG_INFO("---------------------MQ LOOP THREAD EXIT------------------");
    S5LOG_INFO("----------------------------------------------------------");
    LEAVE_FUNC;
    return (void*)(OK);
}


/**
 * initiatize client communication channel
 */
int mq_clnt_ctx_init(const char *conf_file,
                    s5mq_id_type_t id_type,
                    const char* id_value,
                    callbackfunc callback,
                    void *private)
{
	int rc = OK;

    memset(&g_mq_clnt_ctx, 0, sizeof(g_mq_clnt_ctx));
	if(NULL == conf_file || (NULL == id_value))
    {
        S5LOG_ERROR("Failed to do mq_clnt_ctx_init(), reason: invalid parameters.");
        return EINVAL;
    }
	
    strncpy(g_mq_clnt_ctx.self_id, id_value,  MQ_NAME_LEN - 1);

	rc = init_ctx_from_cfg(conf_file, id_type, &g_mq_clnt_ctx);
	if(OK != rc)
	{
		S5LOG_ERROR("Failed to init_ctx_from_cfg.(errno:%d, reason:%s)).",
				errno, zmq_strerror(errno));
		return rc;
	}
    g_mq_clnt_ctx.callback = callback;
    g_mq_clnt_ctx.reconnect_times = 0;
    g_mq_clnt_ctx.private = private;
    if(ID_TYPE_CNDCT == id_type)
    {
        g_mq_clnt_ctx.cndct_status  = CNDCT_ST_FREE;
        g_mq_clnt_ctx.cndct_role    = S5CDT_MASTER;
    }
    else
    {
        g_mq_clnt_ctx.worker_status =  WORKER_ST_FREE;
    }
	/* initialize  zmq ctx, and control socket */
	g_mq_clnt_ctx.sock_ctx = zctx_new();        /* init zctx */
	assert(g_mq_clnt_ctx.sock_ctx);
	g_mq_clnt_ctx.sock_out_ctrlend = zsocket_new(g_mq_clnt_ctx.sock_ctx, ZMQ_PAIR); /* init ctrl end sock */
	assert(g_mq_clnt_ctx.sock_out_ctrlend);     /* init in ctrlend sock */


	/* global variable */
    s5list_init_head(&sendlist);
    s5list_init_head(&recvlist);
    //pthread_mutex_init(&sendmutex, NULL);
    //pthread_mutex_init(&recvmutex, NULL);

	g_is_running		= IS_RUNNING;
    g_hb_lasttime       = 1;
    memset(&broker, 0, sizeof(broker_t));

    /* conductor start broker thread  */
    if(ID_TYPE_CNDCT == g_mq_clnt_ctx.self_type)
    {
        rc = create_detach_thread(thread_broker, &g_mq_clnt_ctx);
        if(OK != rc)
        {
            S5LOG_ERROR("Failed to create clnt_mainthread.(errno:%d, reason:%s)).",
                    errno, zmq_strerror(errno));
            return ERROR;
        }
    }

    rc = create_detach_thread(thread_clnt_event, &g_mq_clnt_ctx);
	if(OK != rc)
	{
		S5LOG_ERROR("Failed to create clnt_mainthread.(errno:%d, reason:%s)).",

				errno, zmq_strerror(errno));
		return ERROR;
	}
    sleep(1);
	rc = zsocket_connect(g_mq_clnt_ctx.sock_out_ctrlend, INSIDE_CTRL_ADDR);
	if(0 != rc)
	{
		S5LOG_ERROR("Failed to do zsocket_connect addr [ %s ](errno:%d, reason:%s)).",
				INSIDE_CTRL_ADDR, errno, zmq_strerror(errno));
	    zsocket_destroy(g_mq_clnt_ctx.sock_ctx, g_mq_clnt_ctx.sock_out_ctrlend);
        return ERROR;
	}
    zmsg_t *regmsg = zmsg_new();
    assert(regmsg);
    zmsg_addstr(regmsg, "DO_REGISTER");
    //zmsg_print(regmsg);
    zmsg_send(&regmsg, g_mq_clnt_ctx.sock_out_ctrlend);
    zmsg_t *regret = my_recv_timeout(g_mq_clnt_ctx.sock_out_ctrlend, MQ_REG_WAIT_TIME*10, "Out control DO_REGISTE");
    if(NULL == regret)
    {
        S5LOG_ERROR("Failed to do register to broker.");
        return ERROR;
    }
    //zmsg_print(regret);
    zmsg_destroy(&regret);
	return OK;
}

int init_ctx_from_cfg(const char *conf_file, s5mq_id_type_t id_type, mq_clnt_ctx_t *ctx)
{
    int     rc                      = 0;

    ctx->self_type = id_type;
    S5LOG_DEBUG("Initialize info g_mq_clnt_ctx->self_type:%d.\n", ctx->self_type);
    if (ID_TYPE_CNDCT == id_type)
    {
        rc = init_cndct_ctx_from_cfg(conf_file, ctx);
        return rc;                                                                                                                                                                           

    }
    else
    {
        rc = init_worker_ctx_from_cfg(conf_file, ctx);
        return rc;
    }
	return OK;
}
int init_cndct_ctx_from_cfg(const char *conf_file, mq_clnt_ctx_t *ctx)
{
    int     rc                      = 0;
    conf_file_t fp                  = NULL;

    fp = conf_open(conf_file);
    if (NULL == fp)
    {
        S5LOG_ERROR("Failed to open s5mq configure file [%s]",  conf_file);
        return S5_CONF_ERR;
    }

    /* 1  get frontend port of broker */
    rc = conf_get_int(fp, "BROKER", "port_frontend", &(ctx->port_frontend));
    if (0 != rc)
    {
        S5LOG_ERROR("Failed to get 'port_frontend' in configure file [%s]", conf_file);
        conf_close(fp);
        return S5_CONF_ERR;
    }
    S5LOG_DEBUG("Initialize info ctx->port_frontend:%d.", ctx->port_frontend);

    /*  2 get backend port of broker */
    rc = conf_get_int(fp, "BROKER", "port_backend", &(ctx->port_backend));
    if (0 != rc)
    {
        S5LOG_ERROR("Failed to get 'port_backend' in configure file [%s]", conf_file);
        conf_close(fp);
        return S5_CONF_ERR;
    }
    S5LOG_DEBUG("Initialize info ctx->port_backend:%d.", ctx->port_backend);

    conf_close(fp);
    return OK;
}
int init_worker_ctx_from_cfg(const char *conf_file, mq_clnt_ctx_t *ctx)
{
    int         rc                      = 0;
    const char  *tmp_str                = NULL;
    conf_file_t fp                      = NULL;


    fp = conf_open(conf_file);
    if (NULL == fp)
    {
        S5LOG_ERROR("Failed to open s5mq configure file [%s]",  conf_file);
        return S5_CONF_ERR;
    }

    /* 2 get srv_ip1 master ip address */
    tmp_str = conf_get(fp,  "BROKER", "ip1_master");
    if ( NULL == tmp_str)
    {
        S5LOG_ERROR("Failed to get 'ip1_master' ip address in configure file [%s]",  conf_file);
        conf_close(fp);
        return S5_CONF_ERR;
    }
    strncpy(ctx->ip1_master, tmp_str, IPV4_ADDR_LEN - 1);
    S5LOG_DEBUG("Initialize info ctx->ip1_master:%s.", ctx->ip1_master);

    /* 3  get srv_ip2 slave ip address */
    tmp_str = conf_get(fp,  "BROKER", "ip2_slave");
    if ( NULL == tmp_str)
    {
        S5LOG_ERROR("Failed to get 'ip2_slave' ip address in configure file [%s]", conf_file);
        conf_close(fp);
        return S5_CONF_ERR;
    }
    strncpy(ctx->ip2_slave, tmp_str, IPV4_ADDR_LEN - 1);
    S5LOG_DEBUG("Initialize info ctx->ip2_slave:%s.", ctx->ip2_slave);

    /* 4  get banckend port of broker */
    rc = conf_get_int(fp, "BROKER", "port_backend", &(ctx->port_backend));
    if (0 != rc)
    {
        S5LOG_ERROR("Failed to get 'port_backend' in configure file [%s]", conf_file);
        conf_close(fp);
        return S5_CONF_ERR;
    }
    S5LOG_DEBUG("Initialize info ctx->port_backend:%d.", ctx->port_backend);

    conf_close(fp);
    return OK;
}

void deal_recv_msg(zmsg_t *zmsg)
{
    char msgtype[MQ_NAME_LEN] = {0};
    char *str = NULL;
    //int size = 0;
    //size = zmsg_size(zmsg);
    //zmsg_print(zmsg);
    //printf("recv msg size:%d.\n", size);

    ENTER_FUNC;
    str = zmsg_popstr(zmsg); /*1 head msgtype */
    assert(str);
    sprintf(msgtype,"%s", str);
    free(str);

    if(!strcmp(msgtype, MSG_TYPE_CTRL_REG))
    {
        deal_reg_msg(zmsg);
    }
    else if(!strcmp(msgtype, MSG_TYPE_HB))
    {
        deal_hb_msg(zmsg);
    }
    else if(!strcmp(msgtype, MSG_TYPE_SRV))
    {
        deal_srv_msg(zmsg);
    }
    else{
        S5LOG_INFO("Invalid msg. Wrong msg size. Drop it.");
    }

    LEAVE_FUNC;
	return;
}

/**
 * send message to broker
 *
 * @param sock, void* zeromq socket
 *
 * @retval 0 for success, otherwize return error code
 */
int send_msg_to_broker(void *sock)
{
	int rc = 0;
	mqmsg_t *tmp_mqmsg = NULL;
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    int count = 0;
    zmsg_t *zmsg = NULL;

    ENTER_FUNC;

    if( TRUE != g_mq_clnt_ctx.connect_status)
    {
        S5LOG_DEBUG("Connection staus is FALSE not connected.Do not send msg to broker.");
        return OK;
    }
   
    s5list_lock(&sendlist);
    S5LIST_FOR_EACH_SAFE(pos, n, count, &sendlist)
    {
        tmp_mqmsg = S5LIST_ENTRY(pos,mqmsg_t,list);
        if (NULL == tmp_mqmsg)
            break;
        zmsg = zmsg_new();
        if (NULL == zmsg)
        {
            S5LOG_ERROR("Failed to zmsg_new.");
            break;
        }
        S5LOG_DEBUG("Information about message will be send id: %d is_asend:%d, need_reply:%d.\n", 
                    tmp_mqmsg->usrdata.s5msg->head.transaction_id, 
                    tmp_mqmsg->msghead.is_asend, 
                    tmp_mqmsg->msghead.need_reply);
        rc = pack_req_mqmsg(tmp_mqmsg, zmsg);
        if (0 != rc)
        {
            S5LOG_ERROR("Failed to package request message.(msgid=%lu).", tmp_mqmsg->msghead.msgid);
            break;
        }
        S5LOG_DEBUG("Send message to broker by zmsg_send().\n");
        //zmsg_print(zmsg);
        zmsg_send(&zmsg, sock);

        s5list_del_ulc(&tmp_mqmsg->list);
        
        if (FALSE == tmp_mqmsg->msghead.need_reply)
        {
            free(tmp_mqmsg->usrdata.s5msg->data);
            free(tmp_mqmsg->usrdata.s5msg);
            free(tmp_mqmsg);
            
            goto out;
        }
         
        free(tmp_mqmsg->usrdata.s5msg->data);
        free(tmp_mqmsg->usrdata.s5msg);

        s5list_lock(&recvlist);
        s5list_pushtail_ulc(&tmp_mqmsg->list, &recvlist);
        s5list_unlock(&recvlist);
    }
out:

    s5list_unlock(&sendlist);

	LEAVE_FUNC;
	return OK;
}


/**
 * S5MQ API, send one message to target
 *
 * @param sendmsg[in]   pf_message_t, message client will send out.
 * @param target_id[in] char*,  target identity which is the identity of message receiver, maybe player01...etc.
 * @param recvmsg[out]   pf_message_t, recevie message
 *
 * @retval return OK for success, otherwize error code returned.
 *
 * NOTE: If the recvmsg is NULL, then this function just send a message, doesn't receive anything.
 *       If recvmsg is not NULL, then when this function returned, recvmsg is the received message.
 *       Usr need to malloc and free the recvmsg or usr just use a local variable.
 */
int mq_clnt_msg_send(const pf_message_t *sendmsg,
                    const char *target_id,
                    pf_message_t *recvmsg,
					int time_out_sec)
{
    int     i = 0;
    int     rc = 0;
    int     recv_flag = 0;
    int     need_reply = TRUE;
    mqmsg_t *tmp_mqmsg = NULL;
	mqmsg_t *mqmsg = NULL;
    /*  sleep 0.1 seccond */
    const struct timespec sleep_value = {0,  100 * NANO_SECOND_MULTIPLIER};

    if (sendmsg == NULL)
    {
        S5LOG_ERROR("Invalid parameter.");
        return ERROR;
    }
#if 0
    if(sendmsg->head.data_len <= 0)
    {
        S5LOG_ERROR("length of sendmsg data is 0.");
        return ERROR;
    }
#endif
    if(NULL == recvmsg)
    {
        need_reply = FALSE;
    }
    S5LOG_TRACE("Will send msg id:%d, data_len: %d.",
                sendmsg->head.transaction_id, sendmsg->head.data_len);
    /* memery will free after send_msg_to_broker */
    mqmsg = (mqmsg_t *)malloc(sizeof(mqmsg_t));
    assert(mqmsg);
    memset(mqmsg, 0, sizeof(mqmsg_t));

	rc = clnt_create_mq_msg(sendmsg, target_id, need_reply, mqmsg);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to create mqmsg.");
        free(mqmsg);
        return ERROR;
    }
    /**
     * If recemsg is NULL, just add sendlist.
     */
    s5list_lock(&sendlist);
    s5list_pushtail_ulc(&mqmsg->list, &sendlist);
    s5list_unlock(&sendlist);
    if(NULL == recvmsg)
    {
        return OK;
    }

	int loop_cnt = time_out_sec > 0 ? 10 * time_out_sec : 10 * MQ_MSG_RECV_WAIT_TIME;
    /**
     * check if return message in recvlist
     */
    for (i = 0; i < loop_cnt; i++)
    {
        pf_dlist_entry_t *pos;
        pf_dlist_entry_t *n;
		int count;
        //list_for_each_safe(pos, n, &recvlist.list) //调用list.h中的list_for_each_safe 函数进行遍历
        s5list_lock(&recvlist);
        S5LIST_FOR_EACH_SAFE(pos, n, count,  &recvlist)
        {
            //tmp_mqmsg = list_entry(pos,mqmsg_t,list); //调用list_entry函数得到相对应的节点
            tmp_mqmsg = S5LIST_ENTRY(pos,mqmsg_t,list); //调用list_entry函数得到相对应的节点
            if (NULL == tmp_mqmsg)
            {
                break;
            }
            if (tmp_mqmsg->msghead.msgid == mqmsg->msghead.msgid  && TRUE == tmp_mqmsg->msghead.recv_flag)
            {
			    S5LOG_DEBUG("Succeed to receive message %lu.\n", tmp_mqmsg->msghead.msgid);

                memcpy(&recvmsg->head, &tmp_mqmsg->usrdata.s5msg->head, sizeof(pf_message_head_t));
                memcpy(&recvmsg->tail, &tmp_mqmsg->usrdata.s5msg->tail, sizeof(pf_message_tail_t));

                if(tmp_mqmsg->usrdata.s5msg->head.data_len > 0)
                {
                    recvmsg->data = malloc((size_t)tmp_mqmsg->usrdata.s5msg->head.data_len);
                    assert(recvmsg->data);
                    memset(recvmsg->data, 0, (size_t)tmp_mqmsg->usrdata.s5msg->head.data_len);
                    memcpy(recvmsg->data, tmp_mqmsg->usrdata.s5msg->data, (size_t)tmp_mqmsg->usrdata.s5msg->head.data_len);
                }
                recv_flag = 1;
				s5list_del_ulc(pos);

                if(tmp_mqmsg->usrdata.s5msg->head.data_len > 0)
                {
                    free(tmp_mqmsg->usrdata.s5msg->data);
                    tmp_mqmsg->usrdata.s5msg->data = NULL;
                }
                free(tmp_mqmsg->usrdata.s5msg);
                tmp_mqmsg->usrdata.s5msg = NULL;
                free(tmp_mqmsg);
                tmp_mqmsg = NULL;
                break;
            }
        }
        s5list_unlock(&recvlist);
        if(recv_flag)
        {
            break;
        }
        nanosleep(&sleep_value, NULL);
    }
    if (recv_flag)
    {
        return OK;
    }
    else
    {
        S5LOG_ERROR("Failed to received msg timeout.\n");
        s5list_lock(&recvlist);
        s5list_del_ulc(&mqmsg->list);
        s5list_unlock(&recvlist);
        free(mqmsg);
        mqmsg = NULL;
        return ERROR;
    }
}


/**
 * API for user asend message synchronized
 *
 * @param sendmsg pf_message_t, message client will send
 * @param recvmsg pf_message_t, message client received
 *
 * @retval 0 for success, othrewize return error code
 */
int mq_clnt_msg_asend(const pf_message_t *sendmsg, const char *target_id)
{
    if (sendmsg == NULL)
    {
        S5LOG_ERROR("Invalid parameter.");
        return ERROR;
    }
#if 0
    if(sendmsg->head.data_len <= 0)
    {
        S5LOG_ERROR("length of sendmsg data is 0.");
        return ERROR;
    }
#endif
    /* memery will free after send_msg_to_broker */
	mqmsg_t *mqmsg = (mqmsg_t *)malloc(sizeof(mqmsg_t));
    assert(mqmsg);
    memset(mqmsg, 0, sizeof(mqmsg_t));

	clnt_create_mq_msg(sendmsg, target_id, FALSE, mqmsg);
    mqmsg->msghead.is_asend = IS_ASEND;

    s5list_lock(&sendlist);
    s5list_pushtail_ulc(&mqmsg->list, &sendlist);
    s5list_unlock(&sendlist);
    return OK;
}

/**
 * create mqmsg_t message from pf_message_t message
 *
 * @param clnt_send_msg pf_message_t, message user will send
 * @param mqmsg mqmsg_t, message will send by ZMQ
 *
 * @retval 0 for success
 */
int clnt_create_mq_msg(const pf_message_t *sendmsg,
                        const char *target_id,
                        int reply_flag,
                        mqmsg_t *mqmsg)
{
    /* create mqmsg header */
    strncpy(mqmsg->msghead.msg_type, MSG_TYPE_SRV, MQ_NAME_LEN - 1);
    mqmsg->msghead.msgid = (uint64)sendmsg->head.transaction_id;
    strncpy(mqmsg->msghead.recver, target_id , MQ_NAME_LEN - 1);
    strncpy(mqmsg->msghead.sender, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1);
    mqmsg->msghead.is_asend = NOT_ASEND;
    mqmsg->msghead.need_reply = reply_flag;
    mqmsg->msghead.timestamp = (unsigned long int)time(NULL);
    mqmsg->msghead.recv_flag = FALSE;

    /* create mqmsg userdata */
    mqmsg->usrdata.s5msg = (pf_message_t *)malloc(sizeof(pf_message_t));
    assert(mqmsg->usrdata.s5msg);
    memset(mqmsg->usrdata.s5msg, 0, sizeof(pf_message_t));

    memcpy(&mqmsg->usrdata.s5msg->head, &sendmsg->head, sizeof(pf_message_head_t));
    memcpy(&mqmsg->usrdata.s5msg->tail, &sendmsg->tail, sizeof(pf_message_tail_t));

    S5LOG_DEBUG("Debug info, send message data_len: %d, id:%lu.\n", sendmsg->head.data_len, mqmsg->msghead.msgid);
    if (sendmsg->head.data_len <= 0)
    {
        mqmsg->usrdata.s5msg->data = NULL;
        goto out;
    }
    mqmsg->usrdata.s5msg->data = malloc((size_t)sendmsg->head.data_len);
    assert(mqmsg->usrdata.s5msg->data);
    memset(mqmsg->usrdata.s5msg->data, 0, (size_t)sendmsg->head.data_len);
    memcpy(mqmsg->usrdata.s5msg->data, sendmsg->data, (size_t)sendmsg->head.data_len);

out:
    return OK;
}

int pack_req_mqmsg(mqmsg_t *mqmsg,   zmsg_t *zmsg)
{
	if (NULL == mqmsg || NULL == zmsg)
	{
		S5LOG_ERROR("Failed to do pack_req_msg. Invalid parameter\n");
		return ERROR;
	}
    /* package mqmsg->msghead*/
	zmsg_addstr(zmsg, mqmsg->msghead.msg_type);             /*1 msg type  */
	zmsg_addstrf(zmsg, "%lu", mqmsg->msghead.msgid);       /*2 msg id */
	zmsg_addstr(zmsg, mqmsg->msghead.sender);               /*3 msg sender */
	zmsg_addstr(zmsg, mqmsg->msghead.recver);               /*4 msg recver  */
	zmsg_addstrf(zmsg, "%d", mqmsg->msghead.is_asend);      /*5 msg is asend */
	zmsg_addstrf(zmsg, "%d", mqmsg->msghead.need_reply);    /*6 msg need reply flag */
    zmsg_addstrf(zmsg, "%lu", mqmsg->msghead.timestamp);   /*5 msg timestamp */

    /* package mqmsg->usrdata */
    zmsg_t *datamsg = zmsg_new();
    if (mqmsg->usrdata.s5msg != NULL)
    {
        pack_s5msg_to_zmsg(mqmsg->usrdata.s5msg, datamsg);
    }
    else if (mqmsg->usrdata.cndct != NULL)
    {
        pack_cndct_to_zmsg(mqmsg->usrdata.cndct, datamsg);
    }
    else if (mqmsg->usrdata.worker != NULL)
    {
        pack_worker_to_zmsg(mqmsg->usrdata.worker, datamsg);
    }
    else
    {
        S5LOG_ERROR("Error mqmsg.");
    }
    zmsg_addmsg(zmsg, &datamsg);
	return OK;
}

int unpack_recv_mqmsg(zmsg_t *zmsg, mqmsg_t *mqmsg)
{
    char *str = NULL;

    ENTER_FUNC;
	if (NULL == zmsg || NULL == mqmsg)
	{
		S5LOG_ERROR("Failed to do pack_send_msg. Invalid parameter.");
        LEAVE_FUNC;
		return ERROR;
	}
    /* unpack message header */

	str = zmsg_popstr(zmsg); /*1 head msgid */
    assert(str);
	mqmsg->msghead.msgid = strtoull(str, NULL, 10);
	free(str);
    S5LOG_DEBUG("Debug information,  received message:id:%lu.\n", mqmsg->msghead.msgid);

	str = zmsg_popstr(zmsg); /*2 head sender */
    assert(str);
	sprintf(mqmsg->msghead.sender,  "%s",  str);
	free(str);

	str = zmsg_popstr(zmsg); /*3 head recver */
    assert(str);
	sprintf(mqmsg->msghead.recver,  "%s",  str);
	free(str);

    str = zmsg_popstr(zmsg); /*4 is_asend */
    assert(str);
    mqmsg->msghead.is_asend = atoi(str);
    free(str);

    str = zmsg_popstr(zmsg); /*5 need_reply */
    assert(str);
    mqmsg->msghead.need_reply = atoi(str);
    free(str);

	str = zmsg_popstr(zmsg); /*6 head msg timestamp */
    assert(str);
	mqmsg->msghead.timestamp = strtoul(str, NULL, 10);
	free(str);

    /* unpack userdata */
    zmsg_t *submsg = NULL;
	submsg = zmsg_popmsg(zmsg); /* usrdata task return */
    assert(submsg);
    mqmsg->usrdata.s5msg = (pf_message_t *)malloc(sizeof(pf_message_t));
    assert(mqmsg->usrdata.s5msg);
    unpack_zmsg_to_s5msg(submsg, mqmsg->usrdata.s5msg);

    zmsg_destroy(&submsg);

    LEAVE_FUNC;
	return OK;
}

/**
 * add reply msg to recvlist
 *
 * @param msg[in] zmsg_t, received message by ZMQ
 *
 */
int add_msg_to_recvlist(mqmsg_t *mqmsg)
{
	int find_flag = 0;
	mqmsg_t *tmp_msg;
	//struct list_head *pos;
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    int count = 0;

    ENTER_FUNC;
	//list_for_each(pos,&recvlist.list) //调用list.h中的list_for_each函数进行遍历
    s5list_lock(&recvlist);
    S5LIST_FOR_EACH_SAFE(pos, n, count, &recvlist)
	{
		//tmp_msg = list_entry(pos,mqmsg_t,list); //调用list_entry函数得到相对应的节点
        tmp_msg = S5LIST_ENTRY(pos,mqmsg_t,list);
        if (NULL == tmp_msg)
        {
            S5LOG_INFO("Info:recvlist is empty.");
            break;
        }
		if (tmp_msg->msghead.msgid == mqmsg->msghead.msgid)
        {
			S5LOG_DEBUG("Add reply msg msgid:%lu to wait list.\n", mqmsg->msghead.msgid);
            tmp_msg->msghead.timestamp = mqmsg->msghead.timestamp;
            strcpy(tmp_msg->msghead.sender, mqmsg->msghead.sender);
            strcpy(tmp_msg->msghead.recver, mqmsg->msghead.recver);

            tmp_msg->usrdata.s5msg = (pf_message_t *)malloc(sizeof(pf_message_t));
            assert(tmp_msg->usrdata.s5msg);
            memset(tmp_msg->usrdata.s5msg, 0, sizeof(pf_message_t));

            memcpy(&tmp_msg->usrdata.s5msg->head, &mqmsg->usrdata.s5msg->head, sizeof(pf_message_head_t));
            memcpy(&tmp_msg->usrdata.s5msg->tail, &mqmsg->usrdata.s5msg->tail, sizeof(pf_message_tail_t));

            S5LOG_DEBUG("Check received data, data_len:%d.\n", mqmsg->usrdata.s5msg->head.data_len);
            /* check if  here tmp_msg data_len is zero */
            if (tmp_msg->usrdata.s5msg->head.data_len > 0)
            {
                tmp_msg->usrdata.s5msg->data = malloc((size_t)mqmsg->usrdata.s5msg->head.data_len);
                assert(tmp_msg->usrdata.s5msg->data);
                memset(tmp_msg->usrdata.s5msg->data, 0, (size_t)mqmsg->usrdata.s5msg->head.data_len);
                memcpy(tmp_msg->usrdata.s5msg->data, mqmsg->usrdata.s5msg->data, (size_t)mqmsg->usrdata.s5msg->head.data_len);
            }
            else
            {
                tmp_msg->usrdata.s5msg->data = NULL;
            }

            tmp_msg->msghead.recv_flag = TRUE;
			find_flag = 1;
			break;
		}

	}
    s5list_unlock(&recvlist);

	if(!find_flag)
    {
		S5LOG_DEBUG("Can not find send msg id in recvlist call callbackfun.");
        g_mq_clnt_ctx.callback(mqmsg->usrdata.s5msg, g_mq_clnt_ctx.private, mqmsg->msghead.sender);
        LEAVE_FUNC;
        return 1;
    }
    else
    {
        LEAVE_FUNC;
	    return OK;
    }
}

/**
 * send heartbeat message to broker
 *
 *
 */
void clnt_send_hb_msg(void *sock)
{

    if((long unsigned int)time(NULL) - g_hb_lasttime <=  MQ_HB_TIME_INTERVAL)
    {
        return;
    }
    ENTER_FUNC;

    if( TRUE != g_mq_clnt_ctx.connect_status)
    {
        S5LOG_DEBUG("Connection status is FALSE not connected.Do not send heartbeat message.");
        return;
    }
    zmsg_t *msg = zmsg_new();
    create_heartbeat_msg(msg);
    //S5LOG_TRACE("Send heartbeat message,  self id is [%s].",  g_mq_clnt_ctx.self_id);
    //zmsg_print(msg);
    zmsg_send(&msg, sock);
    g_hb_lasttime = (unsigned long int)time(NULL);

    LEAVE_FUNC;
    return;
}


/* create heartbeat  message
 *
 *
 */
void create_heartbeat_msg(zmsg_t *zmsg)
{
    mqmsg_t mqmsg;
    zmsg_t *submsg = NULL;

    ENTER_FUNC;

    submsg = zmsg_new();
    assert(submsg);
    memset(&mqmsg, 0, sizeof(mqmsg));

    strncpy(mqmsg.msghead.msg_type, MSG_TYPE_HB, MQ_NAME_LEN - 1);  /* msg type */
    mqmsg.msghead.msgid = 1ULL;                                        /* msg id */
    strncpy(mqmsg.msghead.sender, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1);/* msg sender */
    strncpy(mqmsg.msghead.recver, ID_TYPE_BROKER, MQ_NAME_LEN - 1);            /* msg recver */
    mqmsg.msghead.timestamp = (unsigned long int)time(NULL);                              /* msg timestamp */
    mqmsg.msghead.is_asend = NOT_ASEND;
    

    pack_mqmsg_head(&mqmsg.msghead, zmsg);

    /* initialized message body */
    if(ID_TYPE_CNDCT == g_mq_clnt_ctx.self_type)
    {
        cndct_self_t cndct;
        memset(&cndct, 0, sizeof(cndct_self_t));
        strncpy(cndct.cndct_id, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1);
        cndct.status    = g_mq_clnt_ctx.cndct_status;
        cndct.role      = g_mq_clnt_ctx.cndct_role;
        cndct.lasttime  = (unsigned long int)time(NULL);
        assert(submsg);
        pack_cndct_to_zmsg(&cndct, submsg);

    }
    else
    {
        worker_self_t worker;
        memset(&worker, 0, sizeof(worker));
        strncpy(worker.worker_id, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1);
        worker.status   =  g_mq_clnt_ctx.worker_status;
        worker.lasttime = (unsigned long int)time(NULL);
        pack_worker_to_zmsg(&worker, submsg);
    }
    zmsg_addmsg(zmsg, &submsg);

    LEAVE_FUNC;
    return;
}
/* create register  message
 *
 * @param[in,out] zmsg zmsg_t, czmq message
 * @param[in] id_type id_type_t, type of cluster member
 * @param[in] if type is not ID_TYPE_CNDCT, then flag means if it is reconnect
 *
 */
void create_register_msg(zmsg_t *zmsg,  s5mq_id_type_t id_type, int flag)
{
    if (ID_TYPE_CNDCT == id_type)
    {
        S5LOG_INFO("ID_TYPE is conductor.");
        create_cndct_reg_msg(zmsg);
    }
    else
    {
        S5LOG_INFO("ID_TYPE is worker, id_type:%d.",  g_mq_clnt_ctx.self_type);
        create_worker_reg_msg(zmsg, flag);
    }

    return;
}


zmsg_t * my_recv_timeout(void *sock, int sec,  char *info)
{
    int i = 0;
    zmsg_t *msg = NULL;

    for(i = 0; i < sec; i++)
    {
        msg = zmsg_recv_nowait(sock);
        if(NULL != msg)
        {
            return msg;
        }
        else
        {
            //S5LOG_DEBUG("Wait received message, zmsg_recv_nowait recv NULL i=%d, info:%s.\n",  i,  info);
            sleep(1);
        }
        /**
         * if user call mq_clnt_ctx_destroy my_recv_timeout should return quickly
         */
        char *p = strstr(info, "UN_REGISTER");
        if (NULL == p && STOPPED == g_is_running)
        {
            return NULL;
        }

    }
    return NULL;
}

/**
 * register to broker
 *
 * @param[in] sock void*, socket of dataend
 * @param[in] reconnect_flag  int, TRUE means do reconnect
 *
 * @retval return OK if success,  otherwize return ERROR
 */
int clnt_reg_to_broker(void *sock, s5mq_id_type_t id_type, int reconnect_flag)
{
    int rc = 0;
    if(ID_TYPE_CNDCT == id_type)
    {
        rc = do_cndct_register(sock);
        return rc;
    }
    else
    {
        rc = do_worker_register(sock, reconnect_flag);
        return rc;
    }
}

int do_cndct_register(void *sock)
{
    zmsg_t *zmsg = zmsg_new();
    assert(zmsg);
    S5LOG_DEBUG("Create condcutor register message now.\n");
    create_cndct_reg_msg(zmsg);
    //zmsg_print(zmsg);
    zmsg_send(&zmsg, sock);
    zmsg_t *recv_reg = my_recv_timeout(sock, MQ_REG_WAIT_TIME, "conductor register to broker");
    if (NULL != recv_reg)
    {
        S5LOG_INFO("Conductor register to broker successfully.\n");
        zmsg_print(recv_reg);
        broker.lasttime = (unsigned long int)time(NULL);
        zmsg_destroy(&recv_reg);
        return OK;
    }
    else
    {
        S5LOG_ERROR("Failed to do clnt_reg_to_broker.Time out.\n");
        return ERROR;
    }
}

int do_worker_register(void *sock, int reconnect_flag)
{
    int rc = 0;
    char    *str        = NULL; 
    zmsg_t  *zmsg       = NULL;
    zmsg_t  *recv_reg   = NULL;
    
    zmsg = zmsg_new();
    assert(zmsg);
    create_worker_reg_msg(zmsg, reconnect_flag);
    //zmsg_print(zmsg);
    rc = zmsg_send(&zmsg, sock);
    if(rc)
		S5LOG_ERROR("Register debug info, rc:%d, error:%d, reason:%s", rc,  errno, zmq_strerror(errno));
	else	
		S5LOG_INFO("Register debug info, rc:%d, error:%d, reason:%s", rc,  errno, zmq_strerror(errno));
    if (TRUE == reconnect_flag)
    {
        recv_reg = my_recv_timeout(sock, MQ_REG_WAIT_TIME, "worker register to broker.Reconnect flag is TRUE.");
    }
    else
    {
        recv_reg = my_recv_timeout(sock, MQ_REG_WAIT_TIME, "worker register to broker.");
    }
    if (NULL == recv_reg)
    {
        S5LOG_ERROR("Failed to do clnt_reg_to_broker.Time out.\n");
        return ERROR;
    }
    zmsg_print(recv_reg);
    str = zmsg_popstr(recv_reg);
    assert(str);
    free(str);
    str = zmsg_popstr(recv_reg);
    assert(str);
    char *p = strstr(str, "agree");
    if (NULL != p)
    {
        S5LOG_INFO("Worker [%s] register to broker successfully.\n", g_mq_clnt_ctx.self_id);
        zmsg_print(recv_reg);
        broker.lasttime = (unsigned long int)time(NULL);
        zmsg_destroy(&recv_reg);
        free(str);
        return OK;
    }
    S5LOG_ERROR("Failed to register to broker(worker id: [%s]).Broker return message is '%s'.\n", 
                g_mq_clnt_ctx.self_id,  str);
    free(str);
    return S5_E_NO_MASTER;

}

int check_broker_status(mq_clnt_ctx_t *ctx)
{
    uint64 timenow = (unsigned long int)time(NULL);

    if( (timenow - broker.lasttime) > (MQ_HB_TIMEOUT_TIMES * MQ_HB_TIME_INTERVAL))
    {
        S5LOG_ERROR("Broker is offline, heartbeat timeout.");
        ctx->connect_status = FALSE;
        if(0 == ctx->reconnect_times && (ID_TYPE_CNDCT != ctx->self_type))
        {
            send_mqcluster_change_msg(MQCHANGE_TYPE_BROKER_LEAVE, ctx->ip1_master);
        }
        return ERROR;
    }
    return OK;
}

void deal_srv_msg(zmsg_t *zmsg)
{
    int rc = 0;
    int need_free = FALSE;
	mqmsg_t *mqmsg = NULL;

    ENTER_FUNC;
	mqmsg = (mqmsg_t *)malloc(sizeof(mqmsg_t));
    assert(mqmsg);
    memset(mqmsg, 0, sizeof(mqmsg_t));
    //zmsg_print(zmsg);
	unpack_recv_mqmsg(zmsg, mqmsg);
    S5LOG_DEBUG("Received service message id:%d.\n", mqmsg->usrdata.s5msg->head.transaction_id);
    if (TRUE == mqmsg->msghead.is_asend)
    {
        /* asynchronous message */
        S5LOG_DEBUG("This is a asynchronous message, so do callback().\n");
        need_free = FALSE;
        g_mq_clnt_ctx.callback(mqmsg->usrdata.s5msg, g_mq_clnt_ctx.private, mqmsg->msghead.sender);
    }
    else
    {
        /* synchronous message */
        S5LOG_DEBUG("Do add_msg_to_recvlist.");
        rc = add_msg_to_recvlist(mqmsg);
        if (OK == rc)
        {
            need_free = TRUE;
        }
        else
        {
            need_free = FALSE;
        }
    }
    if (need_free)
    {
        /*  free s5msg data  */
        if (NULL != mqmsg->usrdata.s5msg->data)
        {
            free(mqmsg->usrdata.s5msg->data);
            mqmsg->usrdata.s5msg->data = NULL;
        }
        if (NULL != mqmsg->usrdata.s5msg)
        {
            free(mqmsg->usrdata.s5msg);
            mqmsg->usrdata.s5msg = NULL;
        }
    }
	free(mqmsg);
    mqmsg = NULL;
    zmsg_destroy(&zmsg);

    LEAVE_FUNC;
    return;
}
void deal_hb_msg(zmsg_t *zmsg)
{
    ENTER_FUNC;
    //zmsg_print(zmsg);
    broker.lasttime = (unsigned long int)time(NULL);

    zmsg_destroy(&zmsg);
    LEAVE_FUNC;
    return;
}

void deal_reg_msg(zmsg_t *zmsg)
{
    //printf("received reg msg.\n");
    ENTER_FUNC;
    S5LOG_INFO("Received the return message of register.\n");
    LEAVE_FUNC;
}

int create_detach_thread(pthread_f func,void *args)
{
    pthread_attr_t attr;
    pthread_t tid;
    int     flag;

    flag=pthread_attr_init(&attr);
    if(0 != flag )
    {
        S5LOG_ERROR("Failed to do pthread_attr_init, error:%d.", flag);
        goto out0;
    }
    flag=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(0 != flag )
    {
        S5LOG_ERROR("Failed to do pthread_attr_setdetachstate, error:%d.", flag);
        goto out1;
    }
    flag=pthread_create(&tid,&attr,(pthread_f)func,args);
    if(0 != flag )
    {
        S5LOG_ERROR("Failed to do pthread_create, error:%d.", flag);
    }

out1:
    flag=pthread_attr_destroy(&attr);
    if(0 != flag )
    {
        S5LOG_ERROR("Failed to do pthread_attr_destroy,  error:%d.", flag);
        flag = 0;
    }
out0:
    return flag;
}

/* create worker register  message
 *
 * @param[in,out] zmsg zmsg_t, czmq message
 * @param[in] reconnect_flag int, means if it is reconnect
 *
 */
int create_worker_reg_msg(zmsg_t *zmsg,  int reconnect_flag)
{
    mqmsg_t mqmsg;
    worker_self_t worker;

    memset(&mqmsg, 0, sizeof(mqmsg_t));
    /* initialized message header */
    strncpy(mqmsg.msghead.msg_type, MSG_TYPE_CTRL_REG, MQ_NAME_LEN - 1);   /* msg type */
    mqmsg.msghead.msgid = 1ULL;                                         /* msg id */
    strncpy(mqmsg.msghead.sender, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1); /* msg sender */
    strncpy(mqmsg.msghead.recver, ID_TYPE_BROKER, MQ_NAME_LEN - 1);        /* msg recver */
    mqmsg.msghead.timestamp = (unsigned long int)time(NULL);                               /* msg timestamp */
    mqmsg.msghead.is_asend = NOT_ASEND;

    pack_mqmsg_head(&mqmsg.msghead, zmsg);

    /* initialized message body */
    memset(&worker, 0, sizeof(worker));
    strncpy(worker.worker_id, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1);
    worker.status = WORKER_ST_FREE;
    worker.lasttime = (unsigned long int)time(NULL);
    zmsg_t *submsg = zmsg_new();
    assert(submsg);
    pack_worker_to_zmsg(&worker, submsg);

    zmsg_addmsg(zmsg, &submsg);
    if (TRUE == reconnect_flag)
    {
        zmsg_addstr(zmsg, "worker reconnect register message");
    }

    return OK;
}
int create_cndct_reg_msg(zmsg_t *zmsg)
{
    mqmsg_t mqmsg;
    cndct_self_t cndct;

    memset(&mqmsg, 0, sizeof(mqmsg_t));
    /* initialized message header */
    strncpy(mqmsg.msghead.msg_type, MSG_TYPE_CTRL_REG, MQ_NAME_LEN - 1);	/* msg type */
    mqmsg.msghead.msgid = 1ULL;						                    /* msg id */
    strncpy(mqmsg.msghead.sender, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1); /* msg sender */
    strncpy(mqmsg.msghead.recver, ID_TYPE_BROKER, MQ_NAME_LEN - 1);		/* msg recver */
    mqmsg.msghead.timestamp = (unsigned long int)time(NULL);				                /* msg timestamp */
    mqmsg.msghead.is_asend = NOT_ASEND;

    pack_mqmsg_head(&mqmsg.msghead, zmsg);

    /* initialized message body */
    memset(&cndct, 0, sizeof(cndct_self_t));
    strncpy(cndct.cndct_id, g_mq_clnt_ctx.self_id, MQ_NAME_LEN - 1);
    cndct.status    = CNDCT_ST_FREE;
    cndct.role      = g_mq_clnt_ctx.cndct_role;
    cndct.lasttime  = (unsigned long int)time(NULL);
    zmsg_t *submsg  = zmsg_new();
    assert(submsg);
    pack_cndct_to_zmsg(&cndct, submsg);
    //zmsg_print(submsg);
    zmsg_addmsg(zmsg, &submsg);
    //zmsg_print(zmsg);

    return OK;
}

void mq_set_notify_info(notify_to_mqcluster_t notify_info)
{
    if (NOTIFYMQ_TYPE_SET_CONDUCTOR_ROLE == notify_info.subtype)
    {
        set_cndct_notify_info(notify_info.notify_param.cndct_role);
    }
    else if (NOTIFYMQ_TYPE_SET_WORKER_STATUS == notify_info.subtype)
    {
        set_worker_notify_info(notify_info.notify_param.worker_status);
    }
    return;
}
void set_worker_notify_info(pf_worker_status_t status)
{
    g_mq_clnt_ctx.worker_status = status;
}

void set_cndct_notify_info( pf_conductor_role_t role)
{
    pf_dlist_entry_t    *pos        = NULL;
    pf_dlist_entry_t    *n          = NULL;
    cndct_self_t        *tmp_cndct  = NULL;
    int                 count       = 0;

    g_mq_clnt_ctx.cndct_role = role;

    S5LIST_FOR_EACH_SAFE(pos, n, count, &cndctlist)
    {
        tmp_cndct = S5LIST_ENTRY(pos, cndct_self_t, list);
        tmp_cndct->role = role; 
        S5LOG_DEBUG("Debug information: g_mq_clnt_ctx.cndct_role:%d. tmp_cndct->role:%d.", 
                g_mq_clnt_ctx.cndct_role,  tmp_cndct->role);
    }
    return;
}

int mq_clnt_get_worker_list(pf_dlist_head_t *all_worker_list)
{
	destroy_workerlist_ulc(all_worker_list);

    pf_dlist_entry_t    *tmp_pos    = NULL;
    worker_self_t       *tmp_worker = NULL;
    pf_dlist_entry_t    *outside_n  = NULL;
    int                 count       = 0;
    worker_self_t       *new_worker  = NULL;

	s5list_lock(&workerlist);
	if(workerlist.count <= 0)
	{
		s5list_unlock(&workerlist);
		return -1;
	}

    S5LIST_FOR_EACH_SAFE(tmp_pos, outside_n, count, &workerlist)
    {
        tmp_worker = S5LIST_ENTRY(tmp_pos, worker_self_t, list);
		assert(tmp_worker != NULL);
    
        S5LOG_DEBUG("Add to all_worker_list. %s.\n",  tmp_worker->worker_id);
		new_worker = malloc(sizeof(worker_self_t));
		assert(new_worker);
		memset(new_worker,  0,  sizeof(worker_self_t));
		strncpy(new_worker->worker_id, tmp_worker->worker_id,  MQ_NAME_LEN - 1);
		new_worker->status = tmp_worker->status;
		new_worker->lasttime = tmp_worker->lasttime;
		s5list_pushtail_ulc(&new_worker->list, all_worker_list);
    }
	s5list_unlock(&workerlist);
	return 0;
} 

void do_reconnect(mq_clnt_ctx_t *ctx)
{
	int rc = 0;

	int connect_to_master = TRUE;
	if (ID_TYPE_CNDCT == g_mq_clnt_ctx.self_type)
		connect_to_master = (g_mq_clnt_ctx.cndct_role == S5CDT_MASTER); //conductor only reconnect to self
	else
		connect_to_master = !ctx->is_connect_master;  //switch between master/slave

	if (ctx->reconnect_times > 0)
		sleep(5); //avoid continuous reconnect

	char* target_ip = NULL;
	if (connect_to_master)
	{
		rc = do_connect_to_master(ctx);
		target_ip = ctx->ip1_master;
	}
	else
	{
		rc = do_connect_to_slave(ctx);
		target_ip = ctx->ip2_slave;
	}

	if (OK == rc)
	{
		S5LOG_INFO("Success to reconnect to [%s:%d].", target_ip, ctx->port_backend);
		ctx->is_connect_master = connect_to_master;
		ctx->connect_status = TRUE;
		ctx->reconnect_times = 0;
		send_mqcluster_change_msg(MQCHANGE_TYPE_BROKER_ENTER, target_ip);
		return;
	}
	ctx->is_connect_master = TRUE;
	ctx->connect_status = FALSE;
	ctx->reconnect_times++;
	S5LOG_INFO("Failed to reconnect to [%s:%d].Times of reconnect is:%d.",
		target_ip, ctx->port_backend, ctx->reconnect_times);
	send_mqcluster_change_msg(MQCHANGE_TYPE_RECONNECT_FAIL, target_ip);

}

int do_connect_to_slave(mq_clnt_ctx_t *ctx)
{
    int     rc                  = OK;

    zsocket_disconnect(ctx->sock_dataend, "tcp://%s:%d", ctx->ip1_master,  ctx->port_backend);
    zsocket_set_identity(ctx->sock_dataend,  g_mq_clnt_ctx.self_id);
    zsocket_set_linger(ctx->sock_dataend, 0);
    rc = zsocket_connect(ctx->sock_dataend, "tcp://%s:%d", ctx->ip2_slave, ctx->port_backend);
    if(OK != rc)
    {
        //printf("Failed to do zsocket_connect addr [ %s ](errno:%d, reason:%s)).\n",
        S5LOG_ERROR("Failed to do zsocket_connect addr [tcp://%s:%d](errno:%d, reason:%s)).\n",
                ctx->ip2_slave, ctx->port_backend, errno, zmq_strerror(errno));
        return ERROR;
    }

	rc = clnt_reg_to_broker(ctx->sock_dataend,
		ID_TYPE_CNDCT == g_mq_clnt_ctx.self_type ? ID_TYPE_CNDCT : ID_TYPE_WORKER, TRUE);
	if (OK != rc)
    {
        //printf("Failed to do register to broker.(errno:%d, reason:%s)).\n",
        S5LOG_ERROR("Failed to do register to broker.(errno:%d, reason:%s)).",
                errno, zmq_strerror(errno));
        return ERROR;
    }
    return OK;

}
int do_connect_to_master(mq_clnt_ctx_t *ctx)
{
    int     rc                  = OK;

    zsocket_disconnect(ctx->sock_dataend, "tcp://%s:%d", ctx->ip2_slave, ctx->port_backend);
    zsocket_set_identity(ctx->sock_dataend,  g_mq_clnt_ctx.self_id);
    zsocket_set_linger(ctx->sock_dataend, 0);
	rc = clnt_reg_to_broker(ctx->sock_dataend,
		ID_TYPE_CNDCT == g_mq_clnt_ctx.self_type ? ID_TYPE_CNDCT : ID_TYPE_WORKER, TRUE);
	if (OK != rc)
    {
        //printf("Failed to do zsocket_connect addr [ %s ](errno:%d, reason:%s)).\n",
        S5LOG_ERROR("Failed to do zsocket_connect addr [tcp://%s:%d](errno:%d, reason:%s)).\n",
                ctx->ip1_master, ctx->port_backend, errno, zmq_strerror(errno));
        return ERROR;
    }

    rc = clnt_reg_to_broker(ctx->sock_dataend, ID_TYPE_WORKER, TRUE);
    if(OK != rc)
    {
        //printf("Failed to do register to broker.(errno:%d, reason:%s)).\n",
        S5LOG_ERROR("Failed to do register to broker.(errno:%d, reason:%s)).",
                errno, zmq_strerror(errno));
        return ERROR;
    }
    return OK;

}

/**
 * create s5mq cluster member change message
 *
 * @param[in] msg_type int, define in mqcluster_change_type_t
 * @param[in] id    char*,  for worker it is a broker IP, for conductor it is worker ID
 *
 * @retval OK for success,  otherwize error code returned.
 */
int send_mqcluster_change_msg(int subtype,  char *id)
{
    pf_message_t *s5msg = NULL;
    mqcluster_change_t *msg_cluster_change = NULL;

    msg_cluster_change = (mqcluster_change_t *)malloc(sizeof(mqcluster_change_t));
    assert(msg_cluster_change);
    memset(msg_cluster_change, 0,  sizeof(mqcluster_change_t));

    msg_cluster_change->subtype = (mqcluster_change_type_t)subtype;
    strncpy(msg_cluster_change->id, id, MQ_NAME_LEN - 1);

    s5msg = s5msg_create(sizeof(mqcluster_change_t));
    assert(s5msg);
    s5msg->head.msg_type = MSG_TYPE_MQCLUSTER_CHANGE;
    s5msg->data = msg_cluster_change;

    S5LOG_INFO("S5MQ call callback function to send mqcluster chanage message(type of s5message is:%d, subtype is:%d).",
            (int)MSG_TYPE_MQCLUSTER_CHANGE, subtype);
    g_mq_clnt_ctx.callback(s5msg, g_mq_clnt_ctx.private,  "S5MQ self");

    return OK;
}

