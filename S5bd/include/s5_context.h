#ifndef __S5_CONTEXT_HEADER__
#define __S5_CONTEXT_HEADER__
/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * S5 context - stores tenant context and s5 client env info.
 *
 * S5 context acts as a communication module in s5 client. It has tenant context and entire
 * s5 client configuration info.
 *
 * @author xxx
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "s5_meta.h"
#include "s5list.h"
#include "s5socket.h"
#include "s5log.h"
#include "s5conf.h"
#include "s5errno.h"

/**
 * Conductor entry stores each conductor info configured in s5 configuration file.
 *
 * It is used to construct conductor list, which contains all conductors info configured. Conductor info
 * here includes index of conductor, front ip and front port for s5 client to connect with socket.
 */
typedef struct conductor_entry
{
	s5_dlist_entry_t	list;						///< list hook, with which conductor entry can link together to compose a list
	uint32_t			index;						///< index of conductor, also can be taken as id of conductor
	char				front_ip[IPV4_ADDR_LEN];	///< front ip of conductor, s5 client can communicate with conductor with it and front port
	int					front_port;					///< front port of conductor, s5 client can communicate with conductor with it and front ip
	int					spy_port;					///< spy port of conductor, s5 client will send statistic data to conductor by this port
} s5conductor_entry_t;

/**
 * Object used to record info of s5 context.
 *
 * This struct is for internal use. Content of it will be hidden from users of s5. For now, info of s5 context
 * includes all conductor info configured in s5 configuration file, context of tenant, and s5 configuration info.
 */
typedef struct s5_context
{
	/**
	 * conductor entry list.
	 *
	 * It is parsed from s5 configuration file at start.
	 */
	s5_dlist_head_t		conductor_list;						///< the list of conductor ip, suppose its size is 2
	s5_executor_ctx_t	executor_ctx;							///< tenant context
	char				s5_conf_file[MAX_FILE_PATH_LEN];	///< configuration path
	conf_file_t			s5_conf_obj;						///< configuration object
	char*               assigned_ip;						///< assigned conductor ip
	int					assigned_port;						///< assigned port
} s5_context_t;

/**
 * Initialize s5 context.
 *
 * Initialization of s5 context includes tenant authority verification, configuration file parse, etc.
 * If function successfully returned, s5 context info will be stored in s5ctx. When user does not use
 * it anymore, user should call 's5ctx_release' to release. Or else, memory leak can be expected.
 *
 * @param[in]		s5conf			path of s5 configuration file.
 * @param[in]		tenant_name		name of tenant.
 * @param[in]		passwd			password of tenant.
 * @param[in,out]	s5ctx			context of s5, it will be initalized if funciton successfully
 *									returned.
 *
 * @return	0 on success and negative error code for errors.
 * @return	0					success
 * @retval	-ENOMEM				out of memory.
 * @retval	-EINVAL				The mode of S5 configuration file is invalid; size of configuration 
 *								exceeds 0x40000000 bytes;	
 * @retval	-EACCES				Search permission is denied for one of the directories in the path prefix of
 *								S5 configuration file path.
 * @retval	-ENOMEM				Out of memory (i.e., kernel memory).
 * @retval	-EOVERFLOW			path refers to a file whose size cannot be represented in the type off_t.
 *								This can occur when an application compiled on a 32-bit platform without
 *								-D_FILE_OFFSET_BITS=64 calls stat() on a file whose size exceeds (2<<31)-1 bits.
 * @retval	-EIO				unexpected EOF while reading configuration file, possible concurrent modification may cause it.
 * @retval	-S5_CONF_ERR		configuration file does not conform to configuration rules, and user need check log for detailed info.
 *				
 */
int s5ctx_init(const char* s5conf, const char* tenant_name, const char* passwd, s5_context_t** s5ctx);

/**
 * Release s5 context, and reclaim memory. And after function returns, s5ctx will be NULL.
 *
 * @param[in,out]	s5ctx			context of s5, it will be set to NULL if funciton successfully
 *									returned.
 *
 * @return		0 on success and negative error code for errors.
 */
int s5ctx_release(s5_context_t** s5ctx);

/**
 * Get conductor count in s5 context.^M
 *^M
 * @param[in]   s5ctx   S5 context.^M
 *^M
 * @return      conductor count in s5 context^M
 */
int s5ctx_get_conductor_cnt(const s5_context_t* s5ctx);

/**
 * Get conductor entry with specified index.
 *
 * Index of conductor is fixed and exclusive, so also can be taken as id of conductor, which can
 * exclusively locate a conductor. Index is part of conductor info, and is configured in S5 configuration^M
 * file.^M
 *^M
 * @param[in]   s5ctx       context of S5.^M
 * @param[in]   ent_idx     conductor index.^M
 *^M
 * @return conductor entry on success, otherwise, NULL will be returned.^M
 */
s5conductor_entry_t* s5ctx_get_conductor_entry(const s5_context_t* s5ctx, int ent_idx);

/**
 * Send client request to specified conductor.
 *
 * This function is used to send client request to specified conductor, and synchronously wait until reply
 * comes back from conductor in successful run. Client request object is entirely managed by user. But for
 * reply object, if function successfully returned, and user used it anymore, user needs to free it with
 * 'free()' method.
 *
 * @param[in]		s5_ctx					s5_ctx
 * @param[in]		req						client request
 * @param[in,out]	rpl						reply from conductor
 *
 * @return		0 on success and negative error code for errors.
 * @retval	-ECOMM			Communication error, not get reply.
 */
int s5ctx_send_request(const s5_context_t* s5_ctx, s5_client_req_t* req, s5_clt_reply_t** rpl);

#ifdef __cplusplus
}
#endif

#endif

