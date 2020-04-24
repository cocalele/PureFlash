/**
 *  Copyright 2014-2015 by NETBRIC,  Inc.                                                                                                                                                     
 *  @file  pf_common.h
 *  @brief  s5mq inside message header.
 *          s5mq inside message header
 *    				1 inside macro definition
 *    				2 inside message difinition
 *  @author xiaoguang.fan@netbric.com
 *  @data   2015/09/07 11:18:52
 *
 */
#ifndef MY_MSG_H_H
#define MY_MSG_H_H

#define ERROR   		        -1
#define OK  			        0
#define IS_RUNNING		        1
#define STOPPED			        0
#define IS_ASEND		        1
#define NOT_ASEND	        -1
#define NANO_SECOND_MULTIPLIER  1000000     /* 1 millisecond = 1, 000, 000 Nanoseconds*/

#define ID_TYPE_BROKER          "broker"
#define LOCAL_ADDR              "127.0.0.1"
#define MSG_TYPE_CTRL_REG       "cluster_component_register_msg"
#define MSG_TYPE_CTRL_UNREG     "cluster_component_unregister_msg"
#define MSG_TYPE_HB             "cluster_component_heartbeat_msg"
#define MSG_TYPE_SRV            "cluster_componect_service_msg"

#define MQ_MSG_RECV_WAIT_TIME   6000      /* Time of wait message reply back  */
#define MQ_HB_TIME_INTERVAL     500       /* heartbeat interval */
#define MQ_HB_TIMEOUT_TIMES     300       /* heartbeat timeout times */
#define MQ_RECONNECT_TIMES      865400  /* max times of reconnect  */
#define MQ_REG_WAIT_TIME        500       /* register timeout */
#define MQ_DESTROY_WAIT_TIME    500       /* destroy s5mq when exit user thread wait s5mq thread */

#if (MQ_DEBUG)
#define ENTER_FUNC              S5LOG_TRACE("Enter function [%s][%d]", __FUNCTION__,  __LINE__)
#define LEAVE_FUNC              S5LOG_TRACE("Leave function [%s][%d]", __FUNCTION__,  __LINE__)
#else
    #define ENTER_FUNC
    #define LEAVE_FUNC
#endif


/* msg header for transfer msg*/
typedef struct __mq_head_t_
{
    pf_dlist_entry_t    list;
	char	            msg_type[MQ_NAME_LEN];	///< msg type, it can be 'control' or 'service' or 'heartbeat'
	uint64              msgid;                  ///< msg ID 
	char	            sender[MQ_NAME_LEN];	///< msg sender, always is cluster member ID
	char	            recver[MQ_NAME_LEN];	///< msg receiver, alwasy is cluster member ID
	int		            is_asend;			    ///< 0 is send, 1 is asend
	int		            need_reply;			    ///< 1 need reply , 0 doesn't need reply
	unsigned long int   timestamp;			    ///< sending a message begin this timestamp
    int                 recv_flag;
}mq_head_t;

typedef struct _mq_data_t
{
    cndct_self_t    *cndct;                     ///< when send control message or heartbeat message use
    worker_self_t   *worker;                    ///< when send control message or heartbeat message use
    pf_message_t    *s5msg;                     ///< service message use
}mq_data_t;

typedef struct __mqmsg_t_
{
    pf_dlist_entry_t list;
	mq_head_t msghead;
	mq_data_t usrdata;
}mqmsg_t;

typedef struct __broker_t_
{
    uint64 lasttime;
}broker_t;

typedef struct __mq_clnt_ctx_t
{
	char	ip1_master[IPV4_ADDR_LEN];	///< master IP address about broker
	char	ip2_slave[IPV4_ADDR_LEN];     ///< slave  IP address about broker
	int		port_frontend;	            ///< frontend listen port about broker
	int		port_backend;	            ///< frontend listen port about broker
	zctx_t*	sock_ctx;                   ///< ZMQ communication context
    /** 
     * ZMQ socket, control socket, (inside thread use)
     * communicate bwteen main thread and event thread
     */
    void*	sock_in_ctrlend;
    /**
     * ZMQ socket, control socket,
     * communicate bwteen main thread and event thread
     */
	void*	            sock_out_ctrlend;
	void*	            sock_dataend;		        ///< ZMQ socket communicate to broker
	char	            self_id[MQ_NAME_LEN];       ///< cndct ID or worker ID and so on 
    s5mq_id_type_t           self_type;                  ///< identity type , can be cndct or worker 
    pf_cndct_status_t   cndct_status;               ///< condcutor status
    pf_conductor_role_t cndct_role;                 ///< conductor role(master or slave)
    pf_worker_status_t  worker_status;              ///< worker status
    int                 connect_status;             ///<  connect status,  is connected or not with broker
    int                 is_connect_master;          ///< TRUE means is connnecting master, FALSE means is connecting slave
    int                 reconnect_times;            ///< reconnect times for worker
    void                *private;
    callbackfunc callback;
}mq_clnt_ctx_t;

int send_mqcluster_change_msg(int subtype,   char *id);
#endif
