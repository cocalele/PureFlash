#ifndef __S5_ERRNO_H
#define __S5_ERRNO_H

/**
* Copyright (C), 2014-2015.
* @file
* s5errno type and API definitions.
*
* This file includes all s5error types and interfaces, which are used by S5 modules.
*/

#include <errno.h>

#ifdef __cplusplus
#include <string>
std::string cpp_strerror(int err);
#endif

/**
 * S5 error code start from S5_ESTART, and name refer to S5_Exxxx.
 */
#define	S5_ESTART					512	///< S5 error code start.
#define S5_EMISSALIGN 				(S5_ESTART+0)  	///< data address or length not aligned at 4k.
#define S5_ETENANT_ID_NONEXIST  	(S5_ESTART+1)	///< tenant id is not exist.
#define S5_EQUOTASET_ID_NONEXIST	(S5_ESTART+2)	///< quotaset id is not exist.
#define S5_ENEED_INIT				(S5_ESTART+3)	///< object need to initialize.
#define S5_ENETWORK_EXCEPTION		(S5_ESTART+4)	///< object need to initialize.
#define S5_CONF_ERR					(S5_ESTART+5)	///< S5 config file has problem
#define S5_BIND_ERR					(S5_ESTART+6)	///< Error occurs when bind port and IP

#define S5_INTERNAL_ERR				(S5_ESTART+7)	///< For errors of S5 which may be caused by unfixed bugs

#define S5_MQ_ERR				    (S5_ESTART+100)	///< For errors of S5 which may be caused by s5mq
#define S5_E_NO_MASTER              (S5_ESTART+101) ///< There is no master conductor in cluster, 
                                                    ///< new worker can no register in cluster.

#endif
