/**
 * Copyright (C), 2014-2015.
 * @file  
 *    
 * This file defines the data structure: toedaemon, and defines its initialization and release func.
 */

#ifndef __TOEDAEMON__
#define __TOEDAEMON__

#ifdef __cplusplus
extern "C" {
#endif
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "s5list.h"
#include "s5socket.h"
#include "pthread.h"


/**
 * The data structure for the toe server
 */
typedef struct toedaemon 
{
	int						nic_port_count;			 ///< Nic port count for each nic
	int						tray_set_count;			 ///< The count of tray_set
	unsigned short			daemon_request_port;	 ///< The port of first nic to receive daemon rquest	
	const char* 			s5daemon_conf_file;		 ///< S5daemon conf file
	pthread_t           	reap_socket_thread;      ///< The thread to reap disconnected socket threads
	int						real_nic_count;			 ///< Real nic count;		
	char					mngt_ip[32];
} toedaemon_t;
/**
 * The data structure for afsc
 *
 * This data structure is used to manage the nodes for S5afs
 */
typedef struct afsc_st
{
	uint32_t*				ip_array;  ///< ip array of all nics
	struct s5d_srv_toe*     srv_toe_bd;              ///< The array of s5d_srv_toe	
} afsc_st_t;


#ifdef __cplusplus
}
#endif

#endif	

