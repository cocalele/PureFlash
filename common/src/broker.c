/*
 * =====================================================================================
 *
 *       Filename:  broker.c
 *
 *    Description:  socket broker
 *
 *        Version:  0.1
 *        Created:  09/07/2015 10:58:03 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (),
 *   Organization:
 *
 * =====================================================================================
 */
#include "pf_mq.h"
#include "pf_mq_common.h"
#include "pf_mq_pack_unpack.h"

extern int g_is_running;
extern mq_clnt_ctx_t g_mq_clnt_ctx;

typedef struct __broker_chan_t_
{
    zctx_t  *ctx;
    void    *frontend;
    void    *backend;
} broker_chan_t;

pf_dlist_head_t cndctlist;
pf_dlist_head_t workerlist;

static int mq_broker_chan_init(broker_chan_t *chan);
static int mq_broker_run(broker_chan_t chan,  const void *private);
static void mq_broker_chan_destroy(broker_chan_t *chan);

static void deal_front_msg(zmsg_t *zmsg, broker_chan_t chan);
static void deal_front_ctrl_msg(zmsg_t *zmsg, char *msgtype, broker_chan_t chan);
static void deal_front_hb_msg(zmsg_t *zmsg, broker_chan_t chan);
static void deal_front_service_msg(zmsg_t *zmsg, char *envelope, broker_chan_t chan);
static int check_front_identity(mqmsg_t mqmsg, char *envelope);

static void deal_back_msg(zmsg_t *zmsg, broker_chan_t chan);
static void deal_back_ctrl_msg(zmsg_t *zmsg, char *msgtype, broker_chan_t chan);
static void deal_back_hb_msg(zmsg_t *zmsg, broker_chan_t chan);
static void deal_back_service_msg(zmsg_t *zmsg, char *envelope, broker_chan_t chan);


static int check_clnt_status(char *recver);
static int check_worker_identity(mq_head_t msghead, char *envelope);
static int check_worker_status(char *recver);
static void destroy_workerlist(pf_dlist_head_t *worker_list);
static void destroy_cndctlist(pf_dlist_head_t *conductor_list);
static int check_if_has_master(pf_dlist_head_t cndctlist);
static void reply_worker_register_msg(broker_chan_t chan, char *worker_id, int flag);
static void reply_cndct_reg_msg(broker_chan_t chan, char *cndct_id);

void *thread_broker(void *args)
{
	int rc = 0;

    broker_chan_t chan;
    
    rc = mq_broker_chan_init(&chan);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to mq_broker_chan_init.");
        return NULL;
    }

	rc = mq_broker_run(chan, NULL);
    if (ERROR == rc)
    {
        S5LOG_ERROR("Failed to mq_broker_run.");
    }

    mq_broker_chan_destroy(&chan);
	return NULL;
}

/**
 * initialize broker context, channel
 */
int mq_broker_chan_init(broker_chan_t *chan)
{
	int rc = 0;

    s5list_init_head(&cndctlist);
    s5list_init_head(&workerlist);

    chan->ctx= g_mq_clnt_ctx.sock_ctx;

	chan->frontend = zsocket_new(chan->ctx,  ZMQ_ROUTER);
	assert(chan->frontend);
    zsocket_set_linger(chan->frontend,  0);/* set linger to 0 */
	rc = zsocket_bind(chan->frontend, "tcp://*:%d", g_mq_clnt_ctx.port_frontend);
	if(g_mq_clnt_ctx.port_frontend != rc)
	{
		S5LOG_ERROR("Failed to bind on frontend addr:[tcp://*:%d](rc=%d,errno:%d, reason:%s).",
				g_mq_clnt_ctx.port_frontend, rc,errno, zmq_strerror(errno));
		return -1;
	}

	chan->backend = zsocket_new(chan->ctx,  ZMQ_ROUTER);
	assert(chan->backend);
    zsocket_set_linger(chan->backend,  0); /* set linger to 0 */
	rc = zsocket_bind(chan->backend,  "tcp://*:%d", g_mq_clnt_ctx.port_backend);
	if(g_mq_clnt_ctx.port_backend != rc)
	{
		S5LOG_ERROR("Failed to bind on backend addr:[tcp://*:%d](errno:%d, reason:%s)",
				g_mq_clnt_ctx.port_backend, errno, zmq_strerror(errno));
		return -1;
	}
    return 0;
}

int mq_broker_run(broker_chan_t chan, const void *private)
{
	int rc = 0;
	zmq_pollitem_t items [] = {
		{ chan.frontend,  0,  ZMQ_POLLIN,  0 },
		{ chan.backend,   0,  ZMQ_POLLIN,  0 }
	};

	while(g_is_running)
	{
		rc = zmq_poll(items, 2, 1000*ZMQ_POLL_MSEC);
		if(-1 == rc)
		{
			S5LOG_ERROR("Failed to do zmq_poll(errno=%d, reason:%s).",
					errno, zmq_strerror(errno));
			break;
		}
		S5LOG_TRACE("Trace info: zmq_poll timeout, broker is running.");
		if (items [0].revents & ZMQ_POLLIN)
		{
			zmsg_t * msg = zmsg_recv(chan.frontend);
			S5LOG_TRACE("Broker receive message from frontend.");
            //zmsg_print(msg);
            deal_front_msg(msg, chan);
		}
		if (items [1].revents & ZMQ_POLLIN)
		{
			zmsg_t * msg = zmsg_recv(chan.backend);
			S5LOG_TRACE("Broker receive message from backend.");
            //zmsg_print(msg);
            deal_back_msg(msg, chan);
		}

        //broker_check_cluster_member();
	}
    S5LOG_DEBUG("------------------------");
    S5LOG_DEBUG("   Thread broker exit");
    S5LOG_DEBUG("------------------------");

	return rc;
}

int check_front_identity(mqmsg_t msg, char *envelope)
{
    if (strcmp(envelope, msg.msghead.sender))
    {
        S5LOG_ERROR("Illegal network access.");
        return -1;
    }

    return 0;
}
void deal_front_msg(zmsg_t *zmsg, broker_chan_t chan)
{
    char    *str = NULL;
    char    envelope[MQ_NAME_LEN] = {0};
    char    msgtype[MQ_NAME_LEN] = {0};

    str = zmsg_popstr(zmsg); /* envelope */
    assert(str);
    strncpy(envelope, str, MQ_NAME_LEN - 1);
    free(str);

    str = zmsg_popstr(zmsg); /* msg type  */
    assert(str);
    strncpy(msgtype, str, MQ_NAME_LEN - 1);
    free(str);

    if(!strcmp(MSG_TYPE_CTRL_REG, msgtype) || !strcmp(MSG_TYPE_CTRL_UNREG, msgtype))
    {

        deal_front_ctrl_msg(zmsg, msgtype, chan);
        return;
    }
    else if(!strcmp(MSG_TYPE_HB, msgtype))
    {
        deal_front_hb_msg(zmsg, chan);
        return;
    }
    else if(!strcmp(MSG_TYPE_SRV, msgtype))
    {
        deal_front_service_msg(zmsg, envelope, chan);
        return;
    }
    else
    {
        S5LOG_ERROR("Invalid Message type.Just drop msg.");
        zmsg_destroy(&zmsg);
        return;
    }
}

/**
 * Deal frontend register unregister message,
 *      but for now just register message
 * If already register,just update lasttime in cndctlist.
 *
 *
 */
void deal_front_ctrl_msg(zmsg_t *zmsg, char *msgtype, broker_chan_t chan)
{
    int         rc          = 0;
    int         already_reg = 0;
    //struct      list_head *pos;
    pf_dlist_entry_t        *pos;
    pf_dlist_entry_t        *n;
    mq_head_t   msghead;
    zmsg_t      *submsg     = NULL;
    cndct_self_t *tmp_cndct = NULL;
    cndct_self_t *cndct     = NULL;
    int         count       = 0;

    S5LOG_TRACE("Deal front end control message about register message.\n");
    //zmsg_print(zmsg);

    memset(&msghead, 0, sizeof(mq_head_t));
    rc = unpack_mqmsg_head(zmsg, &msghead);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do unpack msg header.");
        zmsg_destroy(&zmsg);
        return;
    }
    submsg = zmsg_popmsg(zmsg);
    assert(submsg);
    cndct = (cndct_self_t *)malloc(sizeof(cndct_self_t));
    assert(cndct);
    memset(cndct,  0,  sizeof(cndct_self_t));
    /* get register message */
    rc = unpack_zmsg_to_cndct(submsg, cndct);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to unpack_zmsg_to_cndct.rc=%d.", rc);
        zmsg_destroy(&zmsg);
        return;
    }
    zmsg_destroy(&submsg);
    zmsg_destroy(&zmsg);

    //list_for_each_safe(pos,n,&cndctlist.list)
    //s5list_lock(&cndctlist);
    S5LIST_FOR_EACH_SAFE(pos, n, count, &cndctlist)
    {
        //tmp_cndct = list_entry(pos,cndct_self_t,list);
        tmp_cndct = S5LIST_ENTRY(pos, cndct_self_t, list);
        if(!strcmp(tmp_cndct->cndct_id, cndct->cndct_id))
        {
            already_reg = 1;
            tmp_cndct->lasttime = cndct->lasttime;
            tmp_cndct->status = cndct->status;
            tmp_cndct->role = cndct->role;
            S5LOG_INFO("Conductor [%s] already register.",  cndct->cndct_id);
        }
    }
    if (!already_reg)
    {
        //s5list_pushtail_ulc(&(cndct->list), &cndctlist);
        s5list_pushtail(&(cndct->list), &cndctlist);
    }
    //s5list_unlock(&cndctlist);

    zmsg_t *front_reg = zmsg_new();
    assert(front_reg);

    zmsg_pushstr(front_reg, cndct->cndct_id);
    zmsg_addstr(front_reg, "broker_agree_front_register");
    //zmsg_print(front_reg);
    rc = zmsg_send(&front_reg, chan.frontend);
    //S5LOG_DEBUG("Send front register agree message.(rc=%d).",rc);

    return;
}

/**
 * deal front heartbeat message
 *
 * update cndctlist, send heartbeat reply message
 *
 * @msg[in] zmsg_t, received ZMQ msg from frontend
 */
void deal_front_hb_msg(zmsg_t *zmsg, broker_chan_t chan)
{
    int rc = 0;
    int find_flag = 0;
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    mq_head_t msghead;
    cndct_self_t *tmp_clnt;
    cndct_self_t *new_cndct = NULL;
    int count = 0;

    //S5LOG_TRACE("Broker received frontend heartbeat, time(%lu).",  (unsigned long int)time(NULL));

    memset(&msghead, 0, sizeof(mq_head_t));
    rc = unpack_mqmsg_head(zmsg, &msghead);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do unpack msg header.");
        zmsg_destroy(&zmsg);
        return;
    }

    new_cndct = (cndct_self_t *)malloc(sizeof(cndct_self_t));
    assert(new_cndct);
    memset(new_cndct, 0, sizeof(cndct_self_t));

    zmsg_t *submsg = zmsg_popmsg(zmsg);
    assert(submsg);
    rc = unpack_zmsg_to_cndct(submsg, new_cndct);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to unpack worker heartbeat msg.");
        free(new_cndct);
        zmsg_destroy(&submsg);
        zmsg_destroy(&zmsg);
        return;
    }
    zmsg_destroy(&submsg);
    zmsg_destroy(&zmsg);


    //list_for_each_safe(pos, n, &(cndctlist.list))
    S5LIST_FOR_EACH_SAFE(pos, n, count, &cndctlist)
    {
        //tmp_clnt = list_entry(pos,cndct_self_t,list);
        tmp_clnt = S5LIST_ENTRY(pos, cndct_self_t, list);
        if (!strcmp(tmp_clnt->cndct_id, new_cndct->cndct_id))
        {
            tmp_clnt->lasttime  = new_cndct->lasttime;
            tmp_clnt->status    = new_cndct->status;
            tmp_clnt->role      = new_cndct->role;
            find_flag           = 1;
        }
    }

    if (!find_flag)
    {
        S5LOG_ERROR("Invalid heartbeat message. Doesn't register %s.",  new_cndct->cndct_id);
        free(new_cndct);
        return;

    }
    /* send reply heartbeat message to clnt from frontend */
    reply_cndct_reg_msg(chan, new_cndct->cndct_id);
    free(new_cndct);
    return;
}

void reply_cndct_reg_msg(broker_chan_t chan, char *cndct_id)
{
    zmsg_t *hb_rep = NULL;
    
    hb_rep = zmsg_new();
    assert(hb_rep);

    zmsg_addstr(hb_rep, MSG_TYPE_HB);
    zmsg_addstr(hb_rep, "broker reply clnt heartbeat msg");
    zmsg_addstrf(hb_rep, "%lu", (uint64)time(NULL));
    zmsg_pushstr(hb_rep, cndct_id);
    //zmsg_print(hb_rep);
    zmsg_send(&hb_rep, chan.frontend);
    return;
}

void deal_front_service_msg(zmsg_t *zmsg, char *envelope, broker_chan_t chan)
{
    int rc = 0;
    mqmsg_t mqmsg;

    memset(&mqmsg, 0, sizeof(mqmsg_t));
    S5LOG_TRACE("Received frontend service message.");
    //zmsg_print(zmsg);

    zmsg_t *new_msg = zmsg_dup(zmsg);
    assert(new_msg);

    unpack_mqmsg_head(zmsg, &mqmsg.msghead);
    S5LOG_DEBUG("Received frontend service message: msghead:%s, envelope:%s.", mqmsg.msghead.sender, envelope);
    rc = check_front_identity(mqmsg, envelope);
    if (OK != rc)
    {
        zmsg_destroy(&zmsg);
        zmsg_destroy(&new_msg);
        return;
    }
    zmsg_destroy(&zmsg);

    rc = check_worker_status(mqmsg.msghead.recver);
    if (OK != rc)
    {
        S5LOG_ERROR("Worker %s dead,  heartbeat timeout.", mqmsg.msghead.recver);
        /* Tell conductor  by callback that a worker left cluster */
        send_mqcluster_change_msg(MQCHANGE_TYPE_WORKER_LEAVE, mqmsg.msghead.recver);
        zmsg_destroy(&new_msg);
        return;
    }

    zmsg_pushstr(new_msg, MSG_TYPE_SRV);
    zmsg_pushstr(new_msg, mqmsg.msghead.recver);
    S5LOG_DEBUG("Send message to worker.");
    //zmsg_print(new_msg);
    zmsg_send(&new_msg, chan.backend);
    return;
}

void deal_back_msg(zmsg_t *zmsg, broker_chan_t chan)
{
    char    *str = NULL;
    char    envelope[MQ_NAME_LEN] = {0};
    char    msgtype[MQ_NAME_LEN] = {0};
    //int     size = 0;

    //size = zmsg_size(zmsg);
    //printf("back recv msg size: %d.\n", size);
    //zmsg_print(zmsg);

    str = zmsg_popstr(zmsg); /* envelope */
    assert(str);
    strncpy(envelope, str, MQ_NAME_LEN - 1);
    free(str);

    str = zmsg_popstr(zmsg); /* msg type  */
    assert(str);
    strncpy(msgtype, str, MQ_NAME_LEN - 1);
    free(str);

    if(!strcmp(MSG_TYPE_CTRL_REG, msgtype) || !strcmp(MSG_TYPE_CTRL_UNREG, msgtype))
    {

        deal_back_ctrl_msg(zmsg, msgtype, chan);
        return;
    }
    else if(!strcmp(MSG_TYPE_HB, msgtype))
    {
        deal_back_hb_msg(zmsg, chan);
        return;
    }
    else if(!strcmp(MSG_TYPE_SRV, msgtype))
    {
        deal_back_service_msg(zmsg, envelope, chan);
        return;
    }
    else
    {
        S5LOG_ERROR("Invalid Message type.Just drop msg.");
        return;
    }
}

void deal_back_ctrl_msg(zmsg_t *zmsg, char *msgtype, broker_chan_t chan)
{
    int     rc = 0;
    int     already_reg = 0;
    int     is_reconnect = FALSE;
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    worker_self_t *tmp_worker = NULL;
    worker_self_t *new_worker = NULL;
    mq_head_t msghead;
    int count = 0;

    memset(&msghead, 0, sizeof(mq_head_t));

    S5LOG_DEBUG("Received worker register message.");
    //zmsg_print(zmsg);

    rc = unpack_mqmsg_head(zmsg, &msghead);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do unpack msg header.");
        return;
    }
    zmsg_t *submsg = zmsg_popmsg(zmsg);
    assert(submsg);

    new_worker = (worker_self_t *)malloc(sizeof(worker_self_t));
    assert(new_worker);
    memset(new_worker,  0,  sizeof(worker_self_t));
    rc = unpack_zmsg_to_worker(submsg, new_worker);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to unpack_worker_register_msg.rc=%d.", rc);
        zmsg_destroy(&zmsg);
        zmsg_destroy(&submsg);
        free(new_worker);
        return;
    }
    zmsg_destroy(&submsg);

    char *sub_reconnect = zmsg_popstr(zmsg);
    if (NULL == sub_reconnect)
    {
        is_reconnect = FALSE; 
    }
    else
    {
        is_reconnect = TRUE;
    }
    zmsg_destroy(&zmsg);

    /* if is first start, there must be a master in cluster */
    if (TRUE != is_reconnect)
    {
        rc = check_if_has_master(cndctlist);
        if (OK != rc)
        {
            /**
             * there is no master conductor in cluster when worker start 
             * do not let new worker register into cluster
             */
            S5LOG_INFO("Broker denied worker[%s] register, because there " \
                        "is no master conductor in cluster.\n", new_worker->worker_id);
            reply_worker_register_msg(chan, new_worker->worker_id, FALSE);
            free(new_worker);
            return;
        }
    }

    /* do register  */
    //list_for_each_safe(pos,n,&workerlist.list)
    s5list_lock(&workerlist);
    S5LIST_FOR_EACH_SAFE(pos, n, count, &workerlist)
    {
        //tmp_worker = list_entry(pos,worker_self_t,list);
        tmp_worker = S5LIST_ENTRY(pos, worker_self_t, list);
        if(!strcmp(tmp_worker->worker_id, new_worker->worker_id))
        {
            S5LOG_INFO("Worker id :%s already register.\n",  tmp_worker->worker_id);
            already_reg = 1;
            tmp_worker->lasttime = new_worker->lasttime;
            tmp_worker->status = new_worker->status;
        }
    }
    if (!already_reg)
    {
        S5LOG_INFO("Add worker :[%s] into workerlist.\n", new_worker->worker_id);
        s5list_pushtail_ulc(&(new_worker->list), &(workerlist));
        S5LOG_DEBUG("After s5list-pushtail workerlist count:%d.\n", workerlist.count);
    }
    s5list_unlock(&workerlist);
    /*  call callback to tell conductor new worker entered cluster */
    send_mqcluster_change_msg(MQCHANGE_TYPE_WORKER_ENTER, new_worker->worker_id);
    reply_worker_register_msg(chan, new_worker->worker_id, TRUE);

    //free(new_worker);
    return;
}

/**
 *  if there is a master conductor return OK
 *
 * @param[in] cndctlist pf_dlist_head_t, conductor list
 *
 * @retval  return OK if there is a master conductor, otherwize return ERROR
 */
int check_if_has_master(pf_dlist_head_t cndctlist)
{
    pf_dlist_entry_t    *pos        = NULL;
    pf_dlist_entry_t    *n          = NULL;
    int                 count       = 0;
    cndct_self_t        *tmp_cndct  = NULL;

#if 1
    S5LIST_FOR_EACH_SAFE(pos, n, count, &cndctlist)
    {
        tmp_cndct = S5LIST_ENTRY(pos, cndct_self_t, list);
        if (S5CDT_MASTER == tmp_cndct->role) 
        {
            return OK;
        }
    }
#endif
#if 0
    S5LOG_DEBUG("g_mq_clnt_ctx.cndct_role:%d.", g_mq_clnt_ctx.cndct_role);
    if (S5CDT_MASTER == g_mq_clnt_ctx.cndct_role)
    {
        return OK;
    }
#endif
    return ERROR;
}

/**
 * reply worker register message
 *
 *
 */
void reply_worker_register_msg(broker_chan_t chan, char *worker_id, int flag)
{
    zmsg_t *back_reg = NULL;
    
    back_reg = zmsg_new();
    assert(back_reg);

    if (TRUE == flag)
    {
        zmsg_addstr(back_reg, "broker_return_worker_register");
        zmsg_addstr(back_reg, "broker_agree_worker_register");
    }
    else
    {
        zmsg_addstr(back_reg, "broker_return_worker_register");
        zmsg_addstr(back_reg, "broker deny register no master in cluster");
    }
    zmsg_pushstr(back_reg, worker_id);
    S5LOG_DEBUG("Reply backend register message to '%s'.", worker_id);
    //zmsg_print(back_reg);
    zmsg_send(&back_reg, chan.backend);
}
void deal_back_hb_msg(zmsg_t *zmsg, broker_chan_t chan)
{
    int rc = OK;
    mq_head_t msghead;
    int find_flag = 0;
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    worker_self_t *tmp_worker = NULL;
    worker_self_t *new_worker = NULL;
    int count = 0;

    new_worker = (worker_self_t *)malloc(sizeof(worker_self_t));
    assert(new_worker);
    memset(new_worker, 0, sizeof(worker_self_t));

    memset(&msghead, 0, sizeof(mq_head_t));
    rc = unpack_mqmsg_head(zmsg, &msghead);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do unpack msg header.");
        free(new_worker);
        zmsg_destroy(&zmsg);
        return;
    }

    zmsg_t *submsg = zmsg_popmsg(zmsg);
    assert(submsg);

    rc = unpack_zmsg_to_worker(submsg, new_worker);
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to unpack worker heartbeat msg.");
        zmsg_destroy(&submsg);
        zmsg_destroy(&zmsg);
        free(new_worker);
        return;
    }
    zmsg_destroy(&zmsg);
    zmsg_destroy(&submsg);

    //list_for_each_safe(pos, n, &(workerlist.list))
    S5LIST_FOR_EACH_SAFE(pos, n, count, &workerlist)
    {
        //tmp_worker = list_entry(pos,worker_self_t,list);
        tmp_worker = S5LIST_ENTRY(pos, worker_self_t, list);
        if (!strcmp(tmp_worker->worker_id, new_worker->worker_id))
        {
            tmp_worker->status =  new_worker->status;
            tmp_worker->lasttime = new_worker->lasttime;
            find_flag = 1;
            break;
        }
    }
    if(!find_flag)
    {
        S5LOG_ERROR("Invalid heartbeat message. Doesn't register (%s).",
                new_worker->worker_id);
        free(new_worker);
        return;
    }
    /* send reply heartbeat message to clnt from frontend */
    zmsg_t *hb_rep = zmsg_new();
    assert(hb_rep);
    zmsg_addstr(hb_rep, MSG_TYPE_HB);
    zmsg_addstr(hb_rep, "broker reply worker heartbeat msg");
    zmsg_addstr(hb_rep, "broker reply worker heartbeat msg");
    zmsg_addstrf(hb_rep, "%lu", (uint64)time(NULL));
    zmsg_pushstr(hb_rep, new_worker->worker_id);
    //zmsg_print(hb_rep);
    zmsg_send(&hb_rep, chan.backend);
    free(new_worker);
    return;

}


void deal_back_service_msg(zmsg_t *zmsg, char *envelope, broker_chan_t chan)
{
    int rc = 0;
    mq_head_t msghead;

    memset(&msghead, 0, sizeof(mq_head_t));
    //zmsg_print(zmsg);

    /* get message envelope */
    zmsg_t *new_msg = zmsg_dup(zmsg);

    unpack_mqmsg_head(zmsg, &msghead);
    //printf("msghead sender:%s, envelope:%s.\n", msghead.sender, envelope);
    rc = check_worker_identity(msghead, envelope);
    if (OK != rc)
    {
        zmsg_destroy(&zmsg);
        zmsg_destroy(&new_msg);
        return;
    }
    zmsg_destroy(&zmsg);

    rc = check_clnt_status(msghead.recver);
    if (OK != rc)
    {
        S5LOG_INFO("Client [%s] dead.", msghead.recver);
        zmsg_destroy(&new_msg);
        return;
    }
    zmsg_pushstr(new_msg, MSG_TYPE_SRV);
    zmsg_pushstr(new_msg, msghead.recver);
    //zmsg_print(new_msg);
    zmsg_send(&new_msg, chan.frontend);
    return;
}


/**
 * check if recver in the workerlist
 *
 */
int check_worker_status(char *recver)
{
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    worker_self_t *worker;
    int count   = 0;

    S5LIST_FOR_EACH_SAFE(pos, n, count, &workerlist)
    {
        worker = S5LIST_ENTRY(pos, worker_self_t, list);
        if (!strcmp(worker->worker_id, recver))
        {
            S5LOG_TRACE("Debug info: worker_id:%s.\n", worker->worker_id);
            return 0;
        }
    }

    return -1;
}

int check_worker_identity(mq_head_t msghead, char *envelope)
{
    if (strcmp(msghead.sender, envelope))
    {
        S5LOG_ERROR("Invalied worker service message. Wrong send/envelope.");
        return -1;
    }
    return 0;
}
int check_clnt_status(char *recver)
{
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    cndct_self_t *tmp_clnt = NULL;
    int count = 0;

    //list_for_each_safe(pos,n,&cndctlist.list)
    S5LIST_FOR_EACH_SAFE(pos, n, count, &cndctlist)
    {
        //tmp_clnt = list_entry(pos,cndct_self_t,list);
        tmp_clnt = S5LIST_ENTRY(pos, cndct_self_t, list);
        if(!strcmp(tmp_clnt->cndct_id, recver))
        {
            return 0;
        }
    }
    return -1;
}
void mq_broker_chan_destroy(broker_chan_t *chan)
{
    S5LOG_DEBUG("ENTER FUNC");
	zsocket_unbind(chan->frontend, "tcp://*:%d",  g_mq_clnt_ctx.port_frontend);
	zsocket_unbind(chan->backend,  "tcp://*:%d", g_mq_clnt_ctx.port_backend);
    zsocket_destroy(chan->ctx,  chan->frontend);
    zsocket_destroy(chan->ctx,  chan->backend);
    //zctx_destroy(&chan->ctx);

    destroy_cndctlist(&cndctlist);
    destroy_workerlist(&workerlist);

    S5LOG_DEBUG("Broker channel destroy...");
    S5LOG_DEBUG("LEAVE FUNC");
}

void destroy_cndctlist(pf_dlist_head_t *conductor_list)
{
    pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n;
    cndct_self_t *tmp_clnt = NULL;
    int count = 0;

    s5list_lock(conductor_list);
    S5LIST_FOR_EACH_SAFE(pos, n, count, conductor_list)
    {
        //tmp_clnt = list_entry(pos,cndct_self_t,list);
        tmp_clnt = S5LIST_ENTRY(pos, cndct_self_t, list);
        if (NULL == tmp_clnt)
            break;
        s5list_del_ulc(pos);
        free(tmp_clnt);
        tmp_clnt = NULL;
    }
    s5list_unlock(conductor_list);
}

void destroy_workerlist_ulc(pf_dlist_head_t *worker_list)
{
	pf_dlist_entry_t *pos;
    pf_dlist_entry_t *n; 
    worker_self_t *tmp_clnt = NULL;
    int count = 0;
   
    S5LIST_FOR_EACH_SAFE(pos, n, count, worker_list)
    {   
        tmp_clnt = S5LIST_ENTRY(pos, worker_self_t, list);
        if (NULL == tmp_clnt)
            break;
        s5list_del_ulc(pos);
        free(tmp_clnt);
        tmp_clnt = NULL;
    }  
}

void destroy_workerlist(pf_dlist_head_t *worker_list)
{
    s5list_lock(worker_list);
    destroy_workerlist_ulc(worker_list);
	s5list_unlock(worker_list);
}

