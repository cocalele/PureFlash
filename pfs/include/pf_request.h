/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
/**
 * Copyright (C), 2014-2015.
 * @file  
 * This file defines the apis to handle requests.
 */

#ifndef __TOEDAEMON_CACHE_MGR__
#define __TOEDAEMON_CACHE_MGR__


#include "pf_adaptor.h"
#include "s5message.h"
struct toedaemon;
/**
 *  Initialize cache management data for S5afs 
 *
 *  Initialize afsc_st, register spy variables.
 *
 *  @param[in]   tray_set_count int32_t,the total number of tray_set
 * 
 */
void register_spy_variables();
void unregister_spy_variables();

/*  Release cache management data for S5afs 
 *
 *  Release afsc_st, and unregister spy variables.
 *
 *  @return      No return.
 */
void cachemgr_release();

/**
 *  The function to handle write request. 
 *
 *  @param[in]   msg		The write request from client.
 *  @param[in]	 socket		The client socket, used for replying msg.
 *  @return      The length of real write.
 *  @retval 	 >=0 The length of real write.
 *  @retval	     -ENOENT	No avaliable nodes in afs for write request.
 *  @retval		 -ENOMEM	No memory left to allocate variables.
 */
int cachemgr_write_request(pf_message_t *msg, PS5CLTSOCKET socket);

/**
 *  The function to handle read request. 
 *
 *  @param[in]   msg        The read request from client.
 *  @param[in]   socket     The client socket, used for replying msg.
 *  @return      The length of real write.
 *  @retval      >=0 The length of real write.
 *  @retval      -ENOENT    read data does not exist.
 *  @retval      -ENOMEM    No memory left to allocate variables.
 */
int cachemgr_read_request(pf_message_t *msg, PS5CLTSOCKET socket);

/**
 *  The function to handle delete request. 
 *
 *  @param[in]   msg        The delete request from client.
 *  @param[in]   socket     The client socket, used for replying msg.
 *  @return      0			Success.
 */
int cachemgr_block_delete_request(pf_message_t *msg, PS5CLTSOCKET socket);


int cachemgr_nic_client_info_request(pf_message_t *msg, PS5CLTSOCKET socket);
int flash_store_config(struct toedaemon* toe_daemon, conf_file_t fp);


#endif	//__TOEDAEMON_CACHE_MGR__

