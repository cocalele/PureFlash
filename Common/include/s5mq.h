/**
 *  Copyright 2014-2015 by NETBRIC, Inc.
 *  @file   s5mq.h
 *  @brief  This file defines message API of libs5mq.so and give the function description.
 *  @author xiaoguang.fan@netbric.com
 *  @data   2015/09/03
 *
 */
#ifndef __S5_MQ_H
#define __S5_MQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <czmq.h>

#include "s5_meta.h"
#include "s5message.h"
#include "s5list.h"
#include "s5_log.h"

#define MQ_NAME_LEN		1024

/*------------------------------data structure definition--------------------------*/

/**
 * callback function definition for 'mq_clnt_msg_asend' function
 *
 * param[in] outmsg  s5_message_t *, received message.
 * param[in] private  void*, paramters for outmsg.
 * param[out] sender char*, sender identiry of outmsg.
 *
 * NOTE: all asynchronous message call this callback function
 *      User need to free data of outmsg and outmsg self.
 *
 */
typedef void (*callbackfunc)(s5_message_t *outmsg, void *private_arg, const char *sender);

/**
 * Type of cluster member
 */
typedef enum __s5mq_id_type_t
{
    ID_TYPE_CNDCT       = 0,  
    ID_TYPE_WORKER      = 1, 
    ID_TYPE_S5PLAYER      = 2, 
    ID_TYPE_S5STOR      = 3, 
    ID_TYPE_MAX
}s5mq_id_type_t;

/**
 * status type of worker(s5daemon or s5player)
 */
typedef enum _s5_worker_status_t
{
    WORKER_ST_FREE          = 0,   
    WORKER_ST_NORMAL        = 1,   
    WORKER_ST_BUSY          = 2,   
}s5_worker_status_t;

/**
 * status type of condcutor
 * for version1.2 not use
 */
typedef enum _mqcluster_cndct_status_t
{
    CNDCT_ST_FREE           = 0,   
    CNDCT_ST_NORMAL         = 1,   
    CNDCT_ST_BUSY           = 2,   
    CNDCT_ST_DEAD           = 3,  
}s5_cndct_status_t;


/* condcutor list member  */
typedef struct __cndct_self_t
{
    s5_dlist_entry_t list;
    char	cndct_id[MQ_NAME_LEN];
    s5_cndct_status_t status;           ///< conductor status
    s5_conductor_role_t role;           ///< condcutor role
    uint64	lasttime;                   ///< last heartbeat timestamp
}cndct_self_t;

/* worker list member, worker can be player or s5storage */
typedef struct _worker_self_t
{
    s5_dlist_entry_t list;
    char	worker_id[MQ_NAME_LEN];		///< worker ID
    s5_worker_status_t status;          ///< worker status
    uint64  lasttime;                   ///< last heartbeat timestamp
}worker_self_t;

/**
 *   Message type of mqcluster change.
 *   worker: s5daemon or player
 *   broker: conductor(master or slave)
 */
typedef enum  _mqcluster_change_type_t
{
	MQCHANGE_TYPE_WORKER_ENTER 		    = 0,	///< worker enter to mqcluster.
	MQCHANGE_TYPE_WORKER_LEAVE 		    = 1,	///< worker leave from mqcluster.
	MQCHANGE_TYPE_BROKER_ENTER		    = 2,	///< broker enter to mqcluster.
	MQCHANGE_TYPE_BROKER_LEAVE		    = 3,	///< broker leave from mqcluster.
	MQCHANGE_TYPE_RECONNECT_FAIL 	    = 4,	///< worker reconnect to broker failed.
    MQCHANGE_TYPE_RECONNECT_MAXTMTES    = 5, 
	MQCHANGE_TYPE_MAX
}mqcluster_change_type_t;


/** 
  * mqcluster change structure. 
  */
typedef struct _mqcluster_change_t
{
    mqcluster_change_type_t subtype;	///< change type.
    char		id[MQ_NAME_LEN];		///< worker-id or broker-id, to represent the role of mqcluster.
}mqcluster_change_t;

/**
 *   Message type of notify to mqcluster by conductor or daemon.
 */
typedef enum  _notify_to_mqcluster_type_t
{
	NOTIFYMQ_TYPE_SET_CONDUCTOR_ROLE 		=0,	///<conductor  notify mqcluster to set it's role.
	NOTIFYMQ_TYPE_SET_WORKER_STATUS 		=1,	///<conductor  notify mqcluster to set it's role.
	NOTIFYMQ_TYPE_MAX
}notify_to_mqcluster_type_t;

/** 
 * notify to mqcluster worker's status structure. 
 */
typedef struct notify_to_mqcluster
{
    notify_to_mqcluster_type_t subtype;	///< notify type.
	union
	{
		s5_conductor_role_t     cndct_role;     ///< the role of the conductor.
        s5_worker_status_t      worker_status;  ///< worker status
	}notify_param;
}notify_to_mqcluster_t;



/*------------------------Function definition/s5 MQ API definition----------------------*/

/**
 * s5mq API,initialized s5mq message context,
 *
 * @param[in] id_type	id_type_t, type of identity, it can be CONDUCTOR or WORKER.
 * @param[in] conf_file	char*, configure file
 * @param[in] calback	callbackfunc, for all asend message
 * @param[in] private	void*,parameters for calback funciton
 *
 * @retval OK For success, otherwize error code returned.
 *
 * @retval E_MQ_LACK_MEM No more memory to use.
 * @retval E_MQ_LACK_MEM Failed to parse configure file s5mq.conf.
 * @retval E_MQ_REGISTER Failed to register to broker.
 *
 * NOTE: you should call mq_clnt_ctx_destroy() when you want to quit.
 */
int mq_clnt_ctx_init(const char *conf_file, s5mq_id_type_t id_type, const char* id_value, callbackfunc callback, void *private_arg);


/**
 * s5mq API, send one message to target
 *
 * @param[in]	sendmsg   s5_message_t*, message will send out.
 * @param[in]	target_id char*, message receiver, maybe player01, worker01, s5stor01...etc.
 * @param[out]	recvmsg   s5_message_t*, recevie message, caller should free this message.
 * @param[in]	time_out_sec, timeout, in seconds, 0 to use default timeout value
 * @retval OK For success, otherwize error code returned.
 *
 * @retval E_MQ_LACK_MEM	No more memroy to allocate.
 * @retval E_MO_WORKER_DEAD Target worker dead.
 * @retval E_MQ_MSG_TIMEOUT	Received timeout.
 *
 * NOTE: If the recvmsg is NULL, then this function just send a message, doesn't receive anything.
 *       If recvmsg is not NULL, then when this function returned, recvmsg is the received message.
 *       Usr need to malloc and free the recvmsg or usr just use a local variable.
 */
int mq_clnt_msg_send(const s5_message_t *sendmsg, const char *target_id, s5_message_t *recvmsg,
	int time_out_sec);


/**
 * s5mq API, asynchronous send message
 *
 * @param[in] sendmsg	clnt_send_msg_t, the message will be send.
 * @param[in] target_id char*, message receiver, maybe player01, worker01...etc.
 * @param[in] callback	callbackfunc, call back function for message asynchronous send return back.
 * @param[in] private	void*,  parameters for callbackfun,
 *							if no parameter for callback just give it NULL.
 *
 * @retval OK For success, otherwize error code returned.
 * @retval E_MQ_LACK_MEM No more memroy to allocate.
 * @retval E_MO_WORKER_DEAD Target worker dead.
 */
int mq_clnt_msg_asend(const s5_message_t *sendmsg, const char *target_id);

/**
 * get all available worker list
 *
 * @param[out] workerlist worker_self_t, all avilable worker list
 *
 * @retval OK  For success, otherwize error code returned.
 *
 * @retval E_MQ_NO_WORKER  No available worker.
 *
 * NOTE: 
 *      This function just for conductor,  use s5_dlist_init to initialize a 
 *      worker list head then call this function until there are some worker online.
 *      You can call this funciton when you need to get lastest available workers.
 *      You need to destroy this list when you exit.
 */
int mq_clnt_get_worker_list(s5_dlist_head_t*workerlist);

/**
 * s5mq API, destroy client context, when you finish your program call this funciton
 *
 */
void mq_clnt_ctx_destroy(void);

/**
 * S5MQ API set notify infomation
 * For now,  conductor set role,  worker set status
 *
 * param[in] notify_info, notify_to_mqcluster_t notify infomation
 *
 */
void mq_set_notify_info(notify_to_mqcluster_t notify_info);

int send_mqcluster_change_msg(int subtype,   char *id);

#ifdef __cplusplus
}
#endif

#endif
