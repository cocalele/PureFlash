#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>          // For sockaddr_in
#include <errno.h>

#include "libs5bd.h"
#include "internal.h"
#include "s5imagectx.h"
#include "spy.h"
#include "s5_context.h"
#include "s5_meta.h"

S5LOG_INIT("s5bd")

#define FATAL_ERROR "FATAL_ERROR"
#define MAX_VOLUME_SIZE_B 9223372036854775807
#define MAX_VOLUME_SIZE_M (MAX_VOLUME_SIZE_B/1024/1024)
static __thread char __error_str[MAX_ERROR_INFO_LENGTH];

using namespace std;
/*
 * for open volume, jconductor will return a json like:
 *  {
 *      "op":"open_volume_reply",
 *	    "status": "OK",
 *      "volume_name":"myvolname",
 *      "volume_size":10000000,
 *      "volume_id":12345678,
 *      "shard_count":2,
 *      "rep_count":3,
 *      "shards":[
 *               { "index":0, "store_ips":["192.168.3.1", "192.168.3.3"]
 * 			 },
 *               { "index":1, "store_ips":["192.168.3.1", "192.168.3.3"]
 * 			 }
 * 			]
 *   }
 */

class PfClientShardInfo
{
public:
	int index;
	std::vector<std::string> store_ips;
};
class PfClientVolumeInfo
{
public:
	std::string status;
	std::string volume_name;
	uint64_t volume_size;
	uint64_t volume_id;
	int shard_count;
	int rep_count;
	std::vector<PfClientShardInfo> shards;

};


int s5_name_info_check(const char* str)
{
	if (!str || strlen(str) == 0)
	{
		return 0;
	}
	const char* cur_ch = NULL;
	int pos = 0;
	while (pos < strlen(str))
	{
		cur_ch = str + pos;
		if ((*cur_ch >= '0' && *cur_ch <= '9') ||		//'0' ~ '9'
			(*cur_ch >= 'a' && *cur_ch <= 'z') ||		//'a' ~ 'z'
			(*cur_ch >= 'A' && *cur_ch <= 'Z') ||		//'A' ~ 'Z'
			(*cur_ch == '_'))		// '_'
		{
			pos++;
			continue;
		}
		return 0;
	}
	return 1;
}

uint64_t  s5round(uint64_t size, uint64_t alignment)
{
	return ((size + alignment - 1) / alignment)  * alignment;
}

static int s5_login(s5_context_t* s5ctx)
{
	s5_clt_reply_t* meta_reply = NULL;
	s5_client_req_t meta_request;

	memset(&meta_request, 0, sizeof(s5_client_req_t));	
	meta_request.sub_type = CLT_USER_LOGIN;

	int rc = 0;
	rc = key_args_check_and_set(meta_request.executor_ctx.user_name, s5ctx->executor_ctx.user_name, MAX_NAME_LEN);
	if(rc)
		return rc;
	rc = key_args_check_and_set(meta_request.executor_ctx.pass_wd, s5ctx->executor_ctx.pass_wd, MAX_NAME_LEN);
	if (rc)
		return rc;

	rc = s5ctx_send_request(s5ctx, &meta_request, &meta_reply);
	if (rc != 0)
	{
		return rc;
	}
	S5ASSERT(meta_reply);
	S5ASSERT(meta_reply->sub_type == CLT_USER_LOGIN);
	if (meta_reply->result < 0)
	{
		rc = meta_reply->result;
		S5LOG_AND_SET_ERROR("Failed to create io-context, as %s.", meta_reply->reply_info.error_info);
	}
	else
	{
		s5ctx->executor_ctx.role = meta_reply->result;
		rc = 0;
	}
	free(meta_reply);
	return rc;
}

uint32_t s5bd_version()
{
	return 0x00010400;	//"01.04.00"
}

int s5_create_ioctx(const char* tenant_name, const char* pswd, const char* s5config, s5_ioctx_t* s5ioctx)
{
	//init spy thread
	static BOOL spy_thread_started = FALSE;
	if (!spy_thread_started)
	{
		spy_sync_start(2002);//port 2002
		spy_thread_started = TRUE;
	}
	int rc = 0;
	if((rc = key_args_check_with_info(tenant_name, MAX_NAME_LEN, "tenant_name", "s5_create_ioctx")) != 0)
		return rc;
	if (s5_name_info_check(tenant_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'tenant_name' can only be composed of letters or numbers or underlined spaces.", tenant_name);
		return -EINVAL;
	}
	
	if((rc = key_args_check_with_info(pswd, MAX_VERIFY_KEY_LEN, "pswd", "s5_create_ioctx")) != 0)
		return rc;
	if((rc = key_args_check_with_info(s5config, MAX_FILE_PATH_LEN, "s5config", "s5_create_ioctx")) != 0)
		return rc;

	int ret = s5ctx_init(s5config, tenant_name, pswd, (s5_context_t**)s5ioctx);
	if (ret != 0)
	{
		return ret;
	}	

	ret = s5_login((s5_context_t*)(*s5ioctx));
	if (ret != 0)
	{
		s5ctx_release((s5_context_t**)s5ioctx);
		*s5ioctx = NULL;
		return ret;
	}	
	return 0;
}

int s5_release_ioctx(s5_ioctx_t* s5ioctx)
{
	s5_context_t* ioctx = (s5_context_t*)(*s5ioctx);
	int rc = 0;
    rc = s5ctx_release(&ioctx);
	*s5ioctx = NULL;
	return rc;
}

static int _s5_open_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char *volume_name, const char *snap_name, 
					  s5_volume_t *volume, BOOL read_only_or_not)
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
    if (!s5_ioctx)
    {   
        S5LOG_AND_SET_ERROR("Parameter 's5ioctx' cannot be null for s5_open_volume.");
        return -EINVAL;
    }   
	if (s5_ioctx->executor_ctx.role != 1)
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {
            S5LOG_AND_SET_ERROR("Can't open volume of other tenant.");
            return -EPERM;
        }
    }
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to open volume.");
            return -EINVAL;
        }
    }	

	int rc = 0;
    if((rc = key_args_check_with_info(volume_name, MAX_NAME_LEN, "volume_name", "s5_open_volume")) != 0)
        return rc;
    if (s5_name_info_check(volume_name) != 1)
    {
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'volume_name' can only be composed of letters or numbers or underlined spaces.", volume_name);
        return -EINVAL;
    }
    if((rc = args_check_with_info(snap_name, MAX_NAME_LEN, "snap_name", "s5_open_volume")) != 0)
        return rc;
    if (snap_name != NULL && s5_name_info_check(snap_name) != 1)
    {
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'snap_name' can only be composed of letters or numbers or underlined spaces.", snap_name);
        return -EINVAL;
    }   


    if (s5ctx_get_conductor_cnt(s5_ioctx) <= 0)
    {   
        S5LOG_AND_SET_ERROR("Io-context used to create volume is invalid. It is not initialized or initialized failed.");
        return -EINVAL;
    }   
    if(!s5_ioctx->s5_conf_obj)
    {   
        S5LOG_AND_SET_ERROR("s5_open_volume:: s5ctx param is invalid, as s5conf is uninitialized.");
        return -EINVAL;
    }   

    s5_volume_ctx_t *volume_ictx = (s5_volume_ctx_t*)malloc(sizeof(s5_volume_ctx_t));
    if (!volume_ictx)
    {   
        S5LOG_AND_SET_ERROR("Failed to open volume because of no memory space left.");
        return -ENOMEM;
    }   
    memset(volume_ictx, 0, sizeof(s5_volume_ctx_t));

	int ofs = snprintf(volume_ictx->volume_name, MAX_NAME_LEN, "%s", volume_name);
	S5ASSERT(ofs == strlen(volume_name));
	volume_ictx->volume_name[ofs] = 0;
	
	ofs = snprintf(volume_ictx->tenant_name, MAX_NAME_LEN, "%s", tenant_name);
	S5ASSERT(ofs == strlen(tenant_name));
	volume_ictx->tenant_name[ofs] = 0;

	if(snap_name)
	{
		ofs = snprintf(volume_ictx->snap_name, MAX_NAME_LEN, "%s", snap_name);
		volume_ictx->snap_name[ofs] = 0;
	}

	int replica_index;
	for(replica_index = 0; replica_index < volume_ictx->replica_num; replica_index++)
		volume_ictx->replica_ctx_id[replica_index] = -1;
	volume_ictx->s5_context = s5_ioctx;
	volume_ictx->read_only = read_only_or_not;

	char s5bd_setion[64] = {0};
	snprintf(s5bd_setion, 64, "%s", (char*)g_s5bd_setion);
	int _s5_io_depth, _rge_io_depth, _rge_io_max_lbas;
		
	rc = conf_get_int(s5_ioctx->s5_conf_obj, s5bd_setion, (char*)g_s5_io_depth, &_s5_io_depth);
	if(rc)
	{
		S5LOG_ERROR("s5_open_volume:: can not find key(%s) in S5conf(%s) s5_io_depth(%d) as default.", 
			(char*)g_s5_io_depth, s5_ioctx->s5_conf_file, S5_IO_DEPTH);
		_s5_io_depth = S5_IO_DEPTH;
	}
	else
		S5LOG_INFO("s5_open_volume:: open the key(%s) as(%d) in S5conf(%s)", (char*)g_s5_io_depth, _s5_io_depth, s5_ioctx->s5_conf_file);
	rc = conf_get_int(s5_ioctx->s5_conf_obj, s5bd_setion, (char*)g_rge_io_depth, &_rge_io_depth);
	if(rc)
	{
		S5LOG_ERROR("s5_open_volume:: can not find key(%s) in S5conf(%s) s5_io_depth(%d) as default.", 
			(char*)g_rge_io_depth, s5_ioctx->s5_conf_file, S5_IO_DEPTH);
		_rge_io_depth = RGE_IO_DEPTH;
	}
	else
		S5LOG_INFO("s5_open_volume:: open the key(%s) as(%d) in S5conf(%s)", (char*)g_rge_io_depth, _rge_io_depth, s5_ioctx->s5_conf_file);

	rc = conf_get_int(s5_ioctx->s5_conf_obj, s5bd_setion, (char*)g_rge_io_max_lbas, &_rge_io_max_lbas);
	if(rc)
	{
		S5LOG_ERROR("s5_open_volume:: can not find key(%s) in S5conf(%s) s5_io_depth(%d) as default.", 
			(char*)g_rge_io_max_lbas, s5_ioctx->s5_conf_file, S5_IO_DEPTH);
		_rge_io_max_lbas = RGE_BULK_SIZE;
	}
	else
		S5LOG_INFO("s5_open_volume:: open the key(%s) as(%d) in S5conf(%s)", (char*)g_rge_io_max_lbas, _rge_io_max_lbas, s5_ioctx->s5_conf_file);
	
	volume_ictx->session_conf.s5_io_depth = min(_s5_io_depth, S5_IO_DEPTH);
	volume_ictx->session_conf.rge_io_depth = _rge_io_depth;
	volume_ictx->session_conf.rge_io_max_lbas = _rge_io_max_lbas;

	rc = open_volume(volume_ictx);
	if (rc != 0)
	{
		goto FAIL_CLEAN_UP;
	}

	*volume = (s5_volume_t)volume_ictx;
	return 0;
FAIL_CLEAN_UP:
	free(volume_ictx);
	return rc;
}


int s5_open_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char *volume_name, const char *snap_name, s5_volume_t *volume)
{
	return _s5_open_volume(s5ioctx, tenant_name, volume_name, snap_name, volume, FALSE);
}

int s5_open_volume_read_only(const s5_ioctx_t s5ioctx, const char* tenant_name, const char *volume_name, const char *snap_name, s5_volume_t *volume)
{
	return _s5_open_volume(s5ioctx, tenant_name, volume_name, snap_name, volume, TRUE);
}

int s5_close_volume(s5_volume_t* volume)
{
	s5_volume_ctx_t *ictx = (s5_volume_ctx_t*)(*volume);
	if (!ictx)
	{
		return 0;
	}
	
	int rc = close_volume(ictx);
	if(!rc)
	{
		free(ictx);
		*volume = NULL;
	}
	return rc;
}

/* I/O */
ssize_t s5_read_volume(s5_volume_t volume, uint64_t ofs, size_t len, char *buf)
{
	s5_volume_ctx_t *ictx = (s5_volume_ctx_t*)volume;
	return _sio_read(ictx, ofs, len, buf);
}

ssize_t s5_write_volume(s5_volume_t volume, uint64_t ofs, size_t len, const char *buf)
{
	s5_volume_ctx_t *ictx = (s5_volume_ctx_t*)volume;
	if (ictx->read_only || strlen(ictx->snap_name) > 0)
	{
		return -EACCES;
	}
	return _sio_write(ictx, ofs, len, buf);
}

int s5_aio_write_volume(s5_volume_t volume, uint64_t off, size_t len, 
															const char *buf, s5bd_callback_t cb_func, void* cb_arg)
{
	int err_no = 0;
	s5_volume_ctx_t *ictx = (s5_volume_ctx_t*)volume;
	if (ictx->read_only || strlen(ictx->snap_name) > 0)
	{
		return -EACCES;
	}
	err_no = update_io_num_and_len(ictx, off, &len);

	if(0 == err_no)
	{
		s5_aiocompletion_t *comp = s5_aio_create_completion(cb_arg, cb_func, FALSE);
		_aio_write(ictx, off, len, buf, comp);
	}	
	
	return err_no;
}

int s5_aio_read_volume(s5_volume_t volume, uint64_t off, size_t len, char *buf, 
														 s5bd_callback_t cb_func, void* cb_arg)
{
	int err_no = 0;
	s5_volume_ctx_t *ictx = (s5_volume_ctx_t*)volume;

	err_no = update_io_num_and_len(ictx, off, &len);
	if(0 == err_no)
	{
		s5_aiocompletion_t *comp = s5_aio_create_completion(cb_arg, cb_func, FALSE);
		_aio_read(ictx, off, len, buf, comp);
	}
	
	return err_no;
}

#define ROUND_4K(size)  s5round(size, 4096)

static int check_tray_store_num(uint32_t replica_num, int *tray_id, const char** s5store_name)
{
    int tray_num = 0;
    int store_num = 0;
    if((replica_num != 1) && (replica_num != 3))
    {
         S5LOG_AND_SET_ERROR("The replica number must be 1 or 3.");
         return -EINVAL;
    }
    while((s5store_name[store_num] != 0) && (store_num < MAX_REPLICA_NUM))
    {
        store_num++;
    }
 
    int j = 0;
    int i = 0;
    if(store_num != 0)
    {
        if(store_num != replica_num)
        {
            S5LOG_AND_SET_ERROR("The number of store is not equal to replica num.");
            return -EINVAL;
        }
        for(i = 0; i < replica_num; i++)
        {
            for(j = i + 1; j < replica_num; j++)
            {   
                if(strcmp(s5store_name[i], s5store_name[j]) == 0)
                {
                    S5LOG_AND_SET_ERROR("S5store name can not be repeated.");
                    return -EINVAL;
                }
            }
        }
    }

    if(store_num == 0)
    {
        for(i = 1; i < MAX_REPLICA_NUM; ++i)
            {
                if(s5store_name[i] != 0)
                {
                    S5LOG_AND_SET_ERROR("User input parameters must begin with tray_id_0 and store_name_0.");
                    return -EINVAL;
                }
            }
    }
    
    while((tray_id[tray_num] != -1) && (tray_num < MAX_REPLICA_NUM))
    { 
        tray_num++;
    }  
   
    if(tray_num != 0)
    {
        if(tray_num != replica_num)
        {
            S5LOG_AND_SET_ERROR("The number of tray is not equal to replica num.");
            return -EINVAL;
        }
          
    }   
    if(tray_num == 0)
    {   
        for(i = 1; i < MAX_REPLICA_NUM; ++i)
        {
            if(tray_id[i] != -1)
            {
                S5LOG_AND_SET_ERROR("User input parameters must begin with tray_id_0 and store_name_0.");
                return -EINVAL;
            }
        }
    }

    return 0;
}

int s5_create_volume(const s5_ioctx_t s5ioctx, const char*tenant_name, const char* volume_name, uint64_t size, uint64_t iops, uint64_t bw,
				 uint64_t flag,  uint32_t replica_num,  int32_t tray_id[MAX_REPLICA_NUM], const char* s5store_name[MAX_REPLICA_NUM])
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (!s5_ioctx)
	{
		S5LOG_AND_SET_ERROR("It is invalid to to create volume with NULL io-context.");
		return -EINVAL;
	}

	if (s5ctx_get_conductor_cnt(s5_ioctx) <= 0)
	{
		S5LOG_AND_SET_ERROR("Io-context used to create volume is invalid. It is not initialized or initialized failed.");
		return -EINVAL;
	}	
	
    if (s5_ioctx->executor_ctx.role != 1)
    {    
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {    
            S5LOG_AND_SET_ERROR("For common tenant, can only create own volume.");
            return -EINVAL;
        }    
    }    
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to create volume.");
            return -EINVAL;
        }
    }	

	s5_client_req_t volume_req;
	memset(&volume_req, 0, sizeof(s5_client_req_t));
	s5_clt_reply_t* reply;

	volume_req.sub_type = CLT_VOLUME_CREATE;

	//limit the size, avoid overflow in conductor(which use signed 64 type)
	if(size > MAX_VOLUME_SIZE_B)
	{
		S5LOG_AND_SET_ERROR("Invalid volume size. Size must less equal %luM(%luB)", MAX_VOLUME_SIZE_M, MAX_VOLUME_SIZE_B);
		return -EINVAL;
	}

	if(size == 0 || size % S5_OBJ_LEN)
	{
		S5LOG_AND_SET_ERROR("Size of volume to create is invalid. Size must be aligned with %d and more than 0.", S5_OBJ_LEN);
		return -EINVAL;
	}

	int rc = 0;
	if ((rc = check_tray_store_num(replica_num, tray_id, s5store_name)) != 0)
    {
        return rc;
    }
    if ((rc = key_args_check_and_set(volume_req.executor_ctx.user_name, s5_ioctx->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(volume_req.executor_ctx.pass_wd, s5_ioctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid s5 io-context(s5_ioctx_t) for s5_create_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}

    char* quotaset_name = NULL;
	if ((rc = key_args_check_and_set_with_info(volume_req.req_param.volume_create_param.volume_name,
		 volume_name, MAX_NAME_LEN, "volume_name", "s5_create_volume")) != 0 ||
		(rc = args_check_and_set_with_info(volume_req.req_param.volume_create_param.quotaset_name, 
		quotaset_name, MAX_NAME_LEN, "quotaset_name", "s5_create_volume")) != 0 ||
		(rc = key_args_check_and_set_with_info(volume_req.req_param.volume_create_param.tenant_name, 
		tenant_name, MAX_NAME_LEN, "tenant_name", "s5_create_volume")) != 0)
	{
		return rc;
	}

    volume_req.req_param.volume_create_param.replica_count = (int)replica_num;
    int i = 0;
    int j = 0;
    for (i = 0; i < replica_num; i++)
    {
        volume_req.req_param.volume_create_param.tray_id[i] = tray_id[i];
    }

    for (j = 0; j < replica_num; j++)
    {   
        if (s5store_name[j] && s5_name_info_check(s5store_name[j]) != 1)
        {
            S5LOG_AND_SET_ERROR("Parameter '%s' for 's5store_name' can only be composed of letters or numbers or underlined spaces.", s5store_name[j]);
            return -EINVAL;
        }

		if (s5store_name[j] == NULL)
		{
			break;
		}
		safe_strcpy(volume_req.req_param.volume_create_param.s5store_name[j], s5store_name[j], sizeof(volume_req.req_param.volume_create_param.s5store_name[j]));
    }
    
	if (s5_name_info_check(tenant_name) != 1)
    {    
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'tenant_name' can only be composed of letters or numbers or underlined spaces.", tenant_name);
        return -EINVAL;
    } 
	if (s5_name_info_check(volume_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'volume_name' can only be composed of letters or numbers or underlined spaces.", volume_name);
		return -EINVAL;
	}
	if (quotaset_name && s5_name_info_check(quotaset_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'quotaset_name' can only be composed of letters or numbers or underlined spaces.", quotaset_name);
		return -EINVAL;
	}

	if(iops == 0)
	{
		S5LOG_AND_SET_ERROR("Iops of volume to create must be more than 0.");
		return -EINVAL;
	}
	volume_req.req_param.volume_create_param.iops = (int64_t)iops;
	volume_req.req_param.volume_create_param.cbs  = (int64_t)get_cbs_by_iops(iops);
	if(bw == 0)
	{
		S5LOG_AND_SET_ERROR("Band-width of volume to create must be more than 0.");
		return -EINVAL;
	}

    s5_volume_access_property_t access = S5_RW_XX;
	volume_req.req_param.volume_create_param.bw = (int64_t)bw;
	//we set flag to default value 1 for now
	volume_req.req_param.volume_create_param.flag = 1;
	volume_req.req_param.volume_create_param.access = access;
	volume_req.req_param.volume_create_param.size = (int64_t)size;
	S5LOG_DEBUG("Create volume: volume_name:%s,"
		"quotaset_name:%s,"
		"iops:%lu, bw:%lu, flag:%lu",
		volume_name, quotaset_name, iops, bw, flag);

	rc = s5ctx_send_request(s5_ioctx, &volume_req, &reply);
	if (rc != 0)
	{
		return rc;
	}

	S5ASSERT(reply );
	S5ASSERT(reply->sub_type == CLT_VOLUME_CREATE);
	rc = reply->result;
	if (rc != 0)
	{
		S5LOG_AND_SET_ERROR("Failed to create volume(name: %s  tenant: %s), as %s.",
			volume_name, s5_ioctx->executor_ctx.user_name, reply->reply_info.error_info);
	}
	
	free(reply);
	return rc;
}


int s5_list_volume(const s5_ioctx_t s5ioctx, s5_volume_list_t* volume_list)
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (s5_ioctx->executor_ctx.role != 1)
	{
		S5LOG_AND_SET_ERROR("Io context of common tenant cannot be used to list all volumes in S5.");
		return -EPERM;
	}

	s5_clt_reply_t* meta_reply = NULL;
	s5_client_req_t meta_request;
	memset(&meta_request, 0, sizeof(meta_request));

	meta_request.sub_type = CLT_LIST_VOLUMES_OF_CLUSTER;

	int rc = 0;
	if ((rc = key_args_check_and_set(meta_request.executor_ctx.user_name, s5_ioctx->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(meta_request.executor_ctx.pass_wd, s5_ioctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid S5 io-context(s5_ioctx_t) for s5_list_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}

	rc = s5ctx_send_request(s5_ioctx, &meta_request, &meta_reply);
	if (rc != 0)
	{
		return rc;
	}

	S5ASSERT(meta_reply);
	S5ASSERT(meta_reply->sub_type == CLT_LIST_VOLUMES_OF_CLUSTER);
	rc = meta_reply->result;
	if (rc != 0)
	{
		S5LOG_AND_SET_ERROR("List volumes of entire S5 system failed, as %s.", meta_reply->reply_info.error_info);
		goto l_free_exit;
	}

	volume_list->num = meta_reply->num;
	if(volume_list->num > 0)
	{
		volume_list->volumes = (s5_volume_info_t*)malloc(sizeof(s5_volume_info_t) * (size_t)meta_reply->num);
		S5ASSERT(volume_list->volumes);
		memcpy(volume_list->volumes, meta_reply->reply_info.volumes, (size_t)meta_reply->num * sizeof(s5_volume_info_t));
	}
	else
	{
		volume_list->volumes = NULL;
	}
l_free_exit:
	free(meta_reply);
	return rc;
}

int s5_release_volumelist(s5_volume_list_t* volume_list)
{
	if(volume_list->num == 0)
	{
		S5ASSERT(!volume_list->volumes);
	}
	else
	{
		S5ASSERT(volume_list->volumes);
		free(volume_list->volumes);
		volume_list->volumes = NULL;
	}
	return 0;
}

int s5_resize_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, uint64_t size)
{
	if (size == 0 || (size % (4 << 20)))
	{
		S5LOG_AND_SET_ERROR("Invalid new size of volume: %lu, and valid size is multiple of 4M and larger than 0.", size);
		return -EINVAL;
	}
	
	s5_context_t* ioctx = (s5_context_t*)s5ioctx; 
	if (ioctx->executor_ctx.role != 1)
    {
        S5ASSERT(ioctx->executor_ctx.role == 0);
        if (strcmp(ioctx->executor_ctx.user_name, tenant_name) != 0)
        {
            S5LOG_AND_SET_ERROR("For common tenant, can only resize own volume.");
            return -EPERM;
        }
    }
    else
    {
        S5ASSERT(ioctx->executor_ctx.role == 1);
        if (strcmp(ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to resize volume.");
            return -EINVAL;
        }
    }	

	uint64_t old_size = 0;
	s5_volume_t volume=NULL;
	int result = 0;
	result = s5_open_volume(ioctx, tenant_name, volume_name, NULL, &volume);
	if(result < 0)
	{
		S5LOG_ERROR("Failed to open volume:%s! err:%d", volume_name, result);
		s5_close_volume(&volume);
		return result;
	}
	old_size = s5_get_opened_volume_size(volume);
	s5_close_volume(&volume);
	if(size < old_size)
	{
		S5LOG_WARN("The new size is less than existing size, the data maybe lost...");
	}
	s5_client_req_t req; 
	memset(&req, 0, sizeof(req));
	s5_clt_reply_t* reply;

	req.sub_type = CLT_VOLUME_RESIZE;
	int rc = 0;
	if ((rc = key_args_check_and_set(req.executor_ctx.user_name, ioctx->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(req.executor_ctx.pass_wd, ioctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid S5 io-context(s5_ioctx_t) for s5_resize_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}
	if ((rc = key_args_check_and_set_with_info(req.req_param.volume_resize_param.volume_name, volume_name, MAX_NAME_LEN, "volume_name", "s5_resize_volume")) != 0 ||
		(rc = key_args_check_and_set_with_info(req.req_param.volume_resize_param.tenant_name, tenant_name, MAX_NAME_LEN, "tenant_name", "s5_resize_volume")) != 0)
	{
		return rc;
	}

	if (s5_name_info_check(volume_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'volume_name' can only be composed of letters or numbers or underlined spaces.", volume_name);
		return -EINVAL;
	}
	if (s5_name_info_check(tenant_name) != 1)
    {
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'tenant_name' can only be composed of letters or numbers or underlined spaces.", tenant_name);
        return -EINVAL;
    }
	
	req.req_param.volume_resize_param.resize =  size;

	rc = s5ctx_send_request(ioctx, &req, &reply);
	if (rc != 0)
	{   
		return rc;
	}    

	S5ASSERT(reply );
	S5ASSERT(reply->sub_type == CLT_VOLUME_RESIZE);
	rc = reply->result;

	if (rc != 0)
	{  
		S5LOG_AND_SET_ERROR("Failed to resize volume(name: %s  tenant_name: %s) as %s.", 
			volume_name, ioctx->executor_ctx.user_name, reply->reply_info.error_info);
	}

	free(reply);
	return rc;
}

uint64_t s5_get_opened_volume_size(s5_volume_t volume)
{
	s5_volume_ctx_t *ictx = (s5_volume_ctx_t *)volume;
	return ictx->volume_size;
}

int s5_stat_opened_volume(s5_volume_t volume, s5_volume_info_t* volume_info)
{
	s5_clt_reply_t* reply = NULL;
	s5_client_req_t request;
	memset(&request, 0, sizeof(request));

	s5_volume_ctx_t* volume_ctx = (s5_volume_ctx_t*)volume;
	request.sub_type = CLT_VOLUME_STAT;
	int rc = 0;
	if ((rc = key_args_check_and_set(request.executor_ctx.user_name, volume_ctx->s5_context->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(request.executor_ctx.pass_wd, volume_ctx->s5_context->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0 ||
		(rc = key_args_check_and_set(request.req_param.volume_stat_param.tenant_name, volume_ctx->tenant_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(request.req_param.volume_stat_param.volume_name, volume_ctx->volume_name, MAX_NAME_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid S5 io-context(s5_ioctx_t) for s5_stat_opened_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}
	int ofs = 0;
	if (strlen(volume_ctx->snap_name) != 0)
	{
		ofs = snprintf(request.req_param.volume_stat_param.snap_name, MAX_NAME_LEN, "%s", volume_ctx->snap_name);
		S5ASSERT(ofs == strlen(volume_ctx->snap_name));
	}
	request.req_param.volume_stat_param.snap_name[ofs] = 0;
	
	S5LOG_DEBUG("Get_volume_info tenant_name:%s, volume_name:%s",
		volume_ctx->s5_context->executor_ctx.user_name, volume_ctx->volume_name);

	rc = s5ctx_send_request(volume_ctx->s5_context, &request, &reply);
	if (rc != 0)
	{
		return rc;
	}

	S5ASSERT(reply);
	S5ASSERT(reply->sub_type == CLT_VOLUME_STAT);
	rc = reply->result;
	if(rc == 0)
	{
		S5ASSERT(reply->num == 1);
		memcpy(volume_info, reply->reply_info.volumes, sizeof(s5_volume_info_t));
	}
	else
	{
		S5LOG_AND_SET_ERROR("Failed to stat info of volume(name: %s tenant: %s), as %s.", 
			volume_ctx->volume_name, volume_ctx->s5_context->executor_ctx.user_name, reply->reply_info.error_info);
	}

	free(reply);

	return rc;
}

int s5_rename_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char *old_name, const char *new_name)
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (s5_ioctx->executor_ctx.role != 1)
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {
            S5LOG_AND_SET_ERROR("For common tenant, can only rename own volume.");
            return -EPERM;
        }
    }
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to rename volume.");
            return -EINVAL;
        }
    }	

	s5_clt_reply_t* meta_reply = NULL;
	s5_client_req_t meta_request;
	memset(&meta_request, 0, sizeof(meta_request));

	meta_request.sub_type = CLT_VOLUME_RENAME;

	int rc = 0;
	if ((rc = key_args_check_and_set(meta_request.executor_ctx.user_name, s5_ioctx->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(meta_request.executor_ctx.pass_wd, s5_ioctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid S5 io-context(s5_ioctx_t) for s5_rename_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}
	if ((rc = key_args_check_and_set_with_info(meta_request.req_param.volume_rename_param.volume_name, old_name, MAX_NAME_LEN, "old_name", "s5_rename_volume")) != 0 ||
		(rc = key_args_check_and_set_with_info(meta_request.req_param.volume_rename_param.new_name, new_name, MAX_NAME_LEN, "new_name", "s5_rename_volume")) != 0 ||
		(rc = key_args_check_and_set_with_info(meta_request.req_param.volume_rename_param.tenant_name, tenant_name, MAX_NAME_LEN, "tenant_name", "s5_rename_volume")) != 0)
	{
		return rc;
	}
	if (s5_name_info_check(old_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'old_name' can only be composed of letters or numbers or underlined spaces.", old_name);
		return -EINVAL;
	}
	if (s5_name_info_check(new_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'new_name' can only be composed of letters or numbers or underlined spaces.", new_name);
		return -EINVAL;
	}
	if (s5_name_info_check(tenant_name) != 1)
    {
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'tenant_name' can only be composed of letters or numbers or underlined spaces.", tenant_name);
        return -EINVAL;
    }

	rc = s5ctx_send_request(s5_ioctx, &meta_request, &meta_reply);
	if (rc != 0)
	{
		return rc;
	}

	S5ASSERT(meta_reply);
	S5ASSERT(meta_reply->sub_type == CLT_VOLUME_RENAME);
	rc = meta_reply->result;
	if (rc != 0)
	{
		S5LOG_AND_SET_ERROR("Failed to rename volume(name: %s  tenant: %s), as %s.",
			new_name, s5_ioctx->executor_ctx.user_name, meta_reply->reply_info.error_info);
	}	

	free(meta_reply);
	return rc;
}

int s5_delete_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name)
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (s5_ioctx->executor_ctx.role != 1)
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {
            S5LOG_AND_SET_ERROR("For common tenant, it can only delete own volume.");
            return -EPERM;
        }
    }
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to delete volume.");
            return -EINVAL;
        }
    }	

	int rc = 0;

	s5_client_req_t volume_req;
	memset(&volume_req, 0, sizeof(volume_req));
	s5_clt_reply_t* reply;

	volume_req.sub_type = CLT_VOLUME_DELETE;

	if ((rc = key_args_check_and_set(volume_req.executor_ctx.user_name, s5_ioctx->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(volume_req.executor_ctx.pass_wd, s5_ioctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid S5 io-context(s5_ioctx_t) for s5_delete_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}
	if ((rc = key_args_check_and_set_with_info(volume_req.req_param.volume_delete_param.volume_name, volume_name, MAX_NAME_LEN, "volume_name", "s5_delete_volume")) != 0 ||
		(rc = key_args_check_and_set_with_info(volume_req.req_param.volume_delete_param.tenant_name, tenant_name, MAX_NAME_LEN, "tenant_name", "s5_delete_volume")) != 0 )
	{
		return rc;
	}

	if (s5_name_info_check(volume_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'volume_name' can only be composed of letters or numbers or underlined spaces.", volume_name);
		return -EINVAL;
	}
	 if (s5_name_info_check(tenant_name) != 1)
    {
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'tenant_name' can only be composed of letters or numbers or underlined spaces.", tenant_name);
        return -EINVAL;
    }

	rc = s5ctx_send_request(s5_ioctx, &volume_req, &reply);
	if (rc != 0)
	{
		return rc;
	}

	S5ASSERT(reply );
	S5ASSERT(reply->sub_type == CLT_VOLUME_DELETE);
	rc = reply->result;
	if (rc != 0)
	{
		S5LOG_AND_SET_ERROR("Failed to delete volume(name: %s  tenant: %s), as %s.",
			volume_name, s5_ioctx->executor_ctx.user_name, reply->reply_info.error_info);
	}
	
	free(reply);
	return rc;
}

int s5_update_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, const char* new_name, int64_t size, 
				int64_t iops, int64_t bw, int64_t flag)
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (s5_ioctx->executor_ctx.role != 1)
    {    
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {    
            S5LOG_AND_SET_ERROR("For common tenant, it can only update own volume.");
            return -EPERM;
        }    
    }    
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {    
            S5LOG_AND_SET_ERROR("Failed to update volume.");
            return -EINVAL;
        }    
    }
	
	if (size != -1 && size <= 0)
	{
		S5LOG_AND_SET_ERROR("Volume size to update is invalid.");
		return -EINVAL;
	}
	if(size != -1 && size % S5_OBJ_LEN)
	{
		S5LOG_AND_SET_ERROR("Size of volume to update is invalid. Size must be aligned with %d and more than 0.", S5_OBJ_LEN);
		return -EINVAL;
	}

	uint64_t old_size = 0;
	s5_volume_t volume;
	int result = 0;
	result = s5_open_volume(s5_ioctx, tenant_name, volume_name, NULL, &volume);
	if (result < 0)
	{
		S5LOG_ERROR("Failed to open volume:%s! err:%d", volume_name, result);
 		return result;
        }
	old_size = s5_get_opened_volume_size(volume);
	s5_close_volume(&volume);
	if(size < old_size)
	{
		S5LOG_WARN("The new size is less than existing size, the data maybe lost...");
	}	
	if (iops != -1 && iops < 0)
	{
		S5LOG_AND_SET_ERROR("Volume iops to update is invalid.");
		return -EINVAL;
	}
	if (bw != -1 && bw < 0)
	{
		S5LOG_AND_SET_ERROR("Volume bw to update is invalid.");
		return -EINVAL;
	}

	s5_clt_reply_t* volume_reply = NULL;
	s5_client_req_t volume_request;

	memset(&volume_request, 0, sizeof(volume_request));
	volume_request.sub_type = CLT_VOLUME_UPDATE;

	int rc = 0;
	if ((rc = key_args_check_and_set(volume_request.executor_ctx.user_name, s5_ioctx->executor_ctx.user_name, MAX_NAME_LEN)) != 0 ||
		(rc = key_args_check_and_set(volume_request.executor_ctx.pass_wd, s5_ioctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN)) != 0 ||
		(rc = key_args_check_and_set(volume_request.req_param.volume_update_param.volume_info.tenant_name, tenant_name, MAX_NAME_LEN)) != 0)
	{
		S5LOG_AND_SET_ERROR("Invalid S5 io-context(s5_ioctx_t) for s5_update_volume. Maybe it is not initialized or initialized incorrectly.");
		return rc;
	}
	if ((rc = key_args_check_and_set_with_info(volume_request.req_param.volume_update_param.src_name, volume_name, MAX_NAME_LEN, "volume_name", "s5_update_volume")) != 0 ||
		(rc = args_check_and_set_with_info(volume_request.req_param.volume_update_param.volume_info.volume_name, new_name, MAX_NAME_LEN, "new_name", "s5_update_volume")) != 0)
	{
		return rc;
	}

	if (s5_name_info_check(tenant_name) != 1)
    {
        S5LOG_AND_SET_ERROR("Parameter '%s' for 'tenant_name' can only be composed of letters or numbers or underlined spaces.", tenant_name);
        return -EINVAL;
    }
	if (s5_name_info_check(volume_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'volume_name' can only be composed of letters or numbers or underlined spaces.", volume_name);
		return -EINVAL;
	}
	if (new_name && s5_name_info_check(new_name) != 1)
	{
		S5LOG_AND_SET_ERROR("Parameter '%s' for 'new_name' can only be composed of letters or numbers or underlined spaces.", new_name);
		return -EINVAL;
	}
	const char* quotaset_name = NULL;
	if (!quotaset_name)
	{
		rc = args_check_and_set(volume_request.req_param.volume_update_param.volume_info.quotaset_name, "KEEP_UNCHANGE", MAX_NAME_LEN);
		S5ASSERT(rc == 0);
	}
	else if (strlen(quotaset_name) == 0)
	{
		rc = args_check_and_set(volume_request.req_param.volume_update_param.volume_info.quotaset_name, "NULL_QUOTASET", MAX_NAME_LEN);
		S5ASSERT(rc == 0);
	}
	else
	{
		rc = args_check_and_set(volume_request.req_param.volume_update_param.volume_info.quotaset_name, quotaset_name, MAX_NAME_LEN);
		S5ASSERT(rc == 0);
		if (quotaset_name && s5_name_info_check(quotaset_name) != 1)
		{
			S5LOG_AND_SET_ERROR("Parameter '%s' for 'quotaset_name' can only be composed of letters or numbers or underlined spaces.", quotaset_name);
			return -EINVAL;
		}
	}	
	s5_volume_access_property_t access = S5_RW_XX;
	volume_request.req_param.volume_update_param.volume_info.iops = iops;
	volume_request.req_param.volume_update_param.volume_info.cbs = (iops < 0 ? -1 : (int64_t)get_cbs_by_iops((uint64_t)iops));
	volume_request.req_param.volume_update_param.volume_info.bw		= bw;
	//flag will not be updated for now
	volume_request.req_param.volume_update_param.volume_info.flag		= -1;
	volume_request.req_param.volume_update_param.volume_info.access	= access;
	volume_request.req_param.volume_update_param.volume_info.size		= size;

	rc = s5ctx_send_request(s5_ioctx, &volume_request, &volume_reply);
	if (rc != 0)
	{
		return rc;
	}

	S5ASSERT(volume_reply );
	S5ASSERT(volume_reply->sub_type == CLT_VOLUME_UPDATE);
	rc = volume_reply->result;
	if (rc != 0)
	{
		S5LOG_AND_SET_ERROR("Failed to update volume(name: %s  tenant: %s) as %s.",
			volume_name, s5_ioctx->executor_ctx.user_name, volume_reply->reply_info.error_info);
	}	

	free(volume_reply);
	return rc;

}


 int s5_export_image(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* image_file, const char* volume_name)
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (s5_ioctx->executor_ctx.role != 1)
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {
            S5LOG_AND_SET_ERROR("For common tenant, can only export own volume image to image file.");
            return -EPERM;
        }
    }
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to export volume image.");
            return -EINVAL;
        }
    }	

	int ret;
	size_t off;
	ssize_t write_len;
	ssize_t read_len;
	size_t buf_len;
	char* buf;
	int fd;
	size_t size;

	s5_volume_t  volume_ctx;

	ret = s5_open_volume(s5ioctx, tenant_name, volume_name, NULL, &volume_ctx);
	if(ret < 0)
	{
		S5LOG_ERROR("Failed to open volume:%s! err:%d", volume_name, ret);
		return ret;
	}

	size = s5_get_opened_volume_size(volume_ctx);

	fd = open(image_file, O_WRONLY | O_CREAT, 0777);
	if(fd < 0)
	{
		S5LOG_AND_SET_ERROR("Open image file:%s failed! err:%d", image_file, errno);
		ret = -errno;
		s5_close_volume(&volume_ctx);
		return ret;
	}

	buf_len =  S5_OBJ_LEN;
	buf = (char*)malloc(buf_len);
	assert(buf);
	off = 0 ;

	do
	{
		read_len  = s5_read_volume(volume_ctx, off, buf_len, buf);
		if(read_len < 0)
		{
			S5LOG_ERROR("Failed to read:%s volume! off:%lu, len:%lu, err:%lu",
			          volume_name,  off, buf_len ,  read_len);
			ret = (int)read_len;
			return ret;
		}

		if(off + (size_t)read_len > size)
		{
			read_len  =  (ssize_t)(size - off);
		}
		write_len = write(fd, buf, (size_t)read_len);
		if(write_len != read_len)
		{
			S5LOG_AND_SET_ERROR("Failed to write image file:%s! off:%lu, len:%lu, err:%d",
			          image_file,  off, buf_len ,  errno);
			ret =  -errno;
			return ret;
		}

		off  += (size_t)write_len;


	}
	while(off < size);


	free(buf);
	close(fd);
	ret = s5_close_volume(&volume_ctx);

	return ret;


}

 int s5_import_image(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, const char* image_file, uint64_t iops, uint64_t bw, 
						  uint64_t flag, uint32_t replica_num,  int32_t tray_id[MAX_REPLICA_NUM], const char* s5store_name[MAX_REPLICA_NUM])
{
	s5_context_t* s5_ioctx = (s5_context_t*)s5ioctx;
	if (s5_ioctx->executor_ctx.role != 1)
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 0);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) != 0)
        {
            S5LOG_AND_SET_ERROR("For common tenant, can only import image to own volume form image file.");
            return -EPERM;
        }
    }
    else
    {
        S5ASSERT(s5_ioctx->executor_ctx.role == 1);
        if (strcmp(s5_ioctx->executor_ctx.user_name, tenant_name) == 0)
        {
            S5LOG_AND_SET_ERROR("Failed to import volume image.");
            return -EINVAL;
        }
    }	
	int ret;
	size_t off;
	ssize_t write_len;
	ssize_t read_len;
	size_t buf_len;
	char* buf;
	int fd;
	uint64_t size;
	struct stat st;
	s5_volume_t  volume_ctx;
	
	fd = open(image_file, O_RDONLY);
	if(fd < 0)
	{
		S5LOG_AND_SET_ERROR("Failed to open image file:%s! err:%d", image_file, errno);
		ret = -errno;
		return ret;
	}

	ret  =  fstat(fd,  &st);  //get file size
	if(ret == -1)
	{
		S5LOG_AND_SET_ERROR("Failed to fstat:%d,image file:%s, err:%d  ", fd, image_file, errno);
		close(fd);
		return  -errno;
	}
	size = (uint64_t)st.st_size;
	if (st.st_size % (4 << 20) != 0)
	{
		S5LOG_AND_SET_ERROR("Size of image file imported into S5 must be 4M alignment.");
		return -EINVAL;
	}

	//ret = s5_volume_resize(s5_ioctx, tenant_name, volume_name, size);
	ret = s5_create_volume(s5ioctx, tenant_name, volume_name, (uint64_t)st.st_size, iops, bw, flag, replica_num, tray_id, s5store_name);
	if(ret != 0)
	{
		S5LOG_ERROR("Create destination volume(name: %s  tenant: %s) failed, err:%d  ", volume_name, s5_ioctx->executor_ctx.user_name, ret);
		close(fd);
		return ret;
	}

	ret = s5_open_volume(s5ioctx, tenant_name, volume_name, NULL, &volume_ctx);
	if(ret < 0)
	{
		S5LOG_ERROR("Failed to open volume[name: %s tenant: %s]! err:%d", volume_name, s5_ioctx->executor_ctx.user_name, ret);
		return ret;
	}

	buf_len =  S5_OBJ_LEN;
	buf = (char*)malloc(buf_len);
	if(!buf)
	{
		S5LOG_AND_SET_ERROR("Failed to import image file because of no memory left.");
		close(fd);
		s5_close_volume(&volume_ctx);
		return -ENOMEM;
	}

	off = 0;

	do
	{
		read_len  = read(fd, buf, buf_len);
		if(read_len < 0)
		{
			S5LOG_AND_SET_ERROR("Failed to read:%s, off:%lu, len:%lu, err:%d  ",
			          image_file, off, buf_len,  errno);
			ret =  -errno;
			break;
		}

		if(read_len < buf_len)
		{
			read_len = (ssize_t)ROUND_4K((size_t)read_len);
		}

		write_len = s5_write_volume(volume_ctx, off, (size_t)read_len, buf);
		if(write_len != read_len)
		{
			S5LOG_ERROR("Failed to write tenant_name:%s,volume:%s, off:%lu, len:%lu, err:%lu ",
			          s5_ioctx->executor_ctx.user_name,  volume_name,  off, read_len ,  write_len);
			ret = (int)write_len;
			return ret;
		}

		off  += (size_t)write_len;
	}
	while(off < size);

	free(buf);
	close(fd);
	s5_close_volume(&volume_ctx);

	return ret;

}

const char* get_last_error_str()
{
	return __error_str;
}

int set_error_str(const char *fmt , ...)
{
	int res_len = 0;
	va_list ap;  
	va_start(ap , fmt);  
	res_len = vsnprintf(__error_str , MAX_ERROR_INFO_LENGTH, fmt , ap);  
	va_end(ap);  
	if (res_len >= MAX_ERROR_INFO_LENGTH)
	{
		return -EOVERFLOW;
	}	
	return res_len;
}


