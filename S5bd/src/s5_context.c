/*
 *s5_context.c
 *
 */
#include <errno.h>
#include <assert.h>

#include "internal.h"
#include "s5_context.h"
#include "s5conf.h"
#include "s5utils.h"

int parse_s5config_for_conductor_info(const char* s5conf, s5_context_t* ctx)
{
	int rc = 0;
	rc = s5list_init_head(&ctx->conductor_list);
	S5ASSERT(rc == 0);

	//parse s5config to get conductor info
	const char*	conductor_ip = NULL;
	int	conductor_port = -1;
	int conductor_spy_port = -1;
	char conductor_section[64] = {0};
	int buf_offset;
	s5conductor_entry_t* conductor_entry;
	
	uint32_t index = 0;
	while (1)
	{
		if (index >= MAX_CONDUCTOR_CNT)
        {   
            break;
        } 	
	
		buf_offset = snprintf(conductor_section, 64, "%s%d", (char*)g_conductor_section, index);
		conductor_section[buf_offset] = 0;
		conductor_ip = conf_get(ctx->s5_conf_obj, conductor_section, g_conductor_ip_key);
		if(!conductor_ip)
		{
			index++;
			continue;
		}
		if(strlen(conductor_ip) >= IPV4_ADDR_LEN)
		{
			S5LOG_AND_SET_ERROR("Invalid conductor ip in config file(%s) for conductor.%d.", s5conf, index);
			rc = -S5_CONF_ERR;
			return rc;
		}

		rc = conf_get_int(ctx->s5_conf_obj, conductor_section, (char*)g_conductor_front_port, &conductor_port);
		if(rc)
		{
			rc = -S5_CONF_ERR;
			S5LOG_AND_SET_ERROR("Failed to parse port of conductor(%u). Config file(%s) is invalid.", index, s5conf);
			return rc;
		}

		rc = conf_get_int(ctx->s5_conf_obj, conductor_section, (char*)g_conductor_spy_port, &conductor_spy_port);
		if(rc)
        {   
            rc = -S5_CONF_ERR;
            S5LOG_AND_SET_ERROR("Failed to parse spy port of conductor(%u). Config file(%s) is invalid.", 
								index, s5conf);            
			return rc;
        } 	

		conductor_entry = (s5conductor_entry_t*)malloc(sizeof(s5conductor_entry_t));

		if (!conductor_entry)
		{
			S5LOG_AND_SET_ERROR("S5 io-context init failed as no memory space left in client-side.");
			rc = -ENOMEM;
			return rc;
		}
		memset(conductor_entry, 0, sizeof(s5conductor_entry_t));
		buf_offset = snprintf(conductor_entry->front_ip, IPV4_ADDR_LEN, "%s", conductor_ip);
		conductor_entry->front_ip[buf_offset] = 0;
		conductor_entry->front_port = conductor_port;
		conductor_entry->spy_port = conductor_spy_port;
		conductor_entry->index = index++;
		conductor_entry->list.head = NULL;
		rc = s5list_push(&conductor_entry->list, &ctx->conductor_list);
		S5ASSERT(rc == 0);
	}

	if (ctx->conductor_list.count <= 0)
	{
		S5LOG_AND_SET_ERROR("No conductor configured in config file, io-context init failed.");
		rc = -S5_CONF_ERR;
		return rc;
	}
	return rc;
}

int s5ctx_init(const char* s5conf, const char* tenant_name, const char* passwd, s5_context_t** s5ctx)
{
	int rc = 0;
	s5_context_t* ctx = NULL;
	ctx = (s5_context_t*)malloc(sizeof(s5_context_t));
	if (!ctx)
	{
		S5LOG_AND_SET_ERROR("S5 io-context init failed as no memory space left.");
		return -ENOMEM;
	}
	
	memset(ctx, 0, sizeof(s5_context_t));

	S5ASSERT(s5conf && strlen(s5conf) > 0);
	int ofs = 0;
	ofs = snprintf(ctx->s5_conf_file, MAX_FILE_PATH_LEN, "%s", s5conf);
	ctx->s5_conf_file[ofs] = 0;

	ctx->s5_conf_obj = conf_open(s5conf);
	if(!ctx->s5_conf_obj)
	{
		S5LOG_AND_SET_ERROR("Failed to parse S5 config file(%s), please refer to log of s5bd for detailed info.", s5conf);
		rc = -errno;
		return rc;
	}

	//parse s5config to get conductor info	
	rc = parse_s5config_for_conductor_info(s5conf, ctx);
	if(rc < 0)
	{
		goto conductor_list_failed_release;
	}

	S5ASSERT(tenant_name && strlen(tenant_name) > 0 && strlen(tenant_name) < MAX_NAME_LEN);
	ofs = snprintf(ctx->executor_ctx.user_name, MAX_NAME_LEN, "%s", tenant_name);
	ctx->executor_ctx.user_name[ofs] = 0;

	S5ASSERT(passwd && strlen(passwd) > 0 && strlen(passwd) < MAX_VERIFY_KEY_LEN);
	ofs = snprintf(ctx->executor_ctx.pass_wd, MAX_VERIFY_KEY_LEN, "%s", passwd);
	ctx->executor_ctx.pass_wd[ofs] = 0;

	*s5ctx = ctx;
	return 0;

	s5_dlist_entry_t* pos;
conductor_list_failed_release:
    pos = s5list_pop(&ctx->conductor_list);
    while (pos)
    {   
        s5conductor_entry_t* conductor_ent = S5LIST_ENTRY(pos, s5conductor_entry_t, list);
        free(conductor_ent);
        conductor_ent = NULL;
        pos = s5list_pop(&ctx->conductor_list);
    }   
    free(ctx);
    ctx = NULL;
    return rc; 
}

int s5ctx_release(s5_context_t** s5ctx)
{
	s5_context_t* ctx = *s5ctx;
	if (!ctx)
	{
		S5LOG_WARN("Ioctx of s5manager to release is not exist.");
		return -EINVAL;
	}
	conf_close(ctx->s5_conf_obj);
	
	s5_dlist_entry_t* pos;
	
	pos = s5list_pop(&ctx->conductor_list);
	while (pos)
	{
		s5conductor_entry_t* conductor_ent = S5LIST_ENTRY(pos,s5conductor_entry_t,list);
		free(conductor_ent);
		conductor_ent = NULL;
		pos = s5list_pop(&ctx->conductor_list);
	}

	free(ctx);
	ctx = NULL;
	return 0;
}

static int send_request_to_conductor(const s5_context_t* s5ctx, s5_client_req_t* req, s5_clt_reply_t** rpl, BOOL reverse)
{
	PS5CLTSOCKET clt_socket = s5socket_create(SOCKET_TYPE_CLT, 0, NULL);
    if(clt_socket == NULL)
	{   
		S5LOG_AND_SET_ERROR("Failed to create socket to connect to conductor. Please check system memory available.");
		return -ENOMEM;
	}

    S5ASSERT(req != NULL);
    int32 rc = 0;
    static int tid = 0; 

	if(s5ctx->assigned_ip != NULL)
	{
		rc = s5socket_connect(clt_socket, 
                              s5ctx->assigned_ip, NULL,
							  (uint16_t)s5ctx->assigned_port, (uint16_t)0, 
                              RCV_TYPE_MANUAL, CONNECT_TYPE_TEMPORARY);		
	}
	else
	{
		int conductor_cnt = s5ctx_get_conductor_cnt(s5ctx);
    	S5ASSERT(conductor_cnt > 0); 
    
   		s5conductor_entry_t* conductor_0_entry = reverse ? s5ctx_get_conductor_entry(s5ctx, 1) : s5ctx_get_conductor_entry(s5ctx, 0); 
    	s5conductor_entry_t* conductor_1_entry = reverse ? s5ctx_get_conductor_entry(s5ctx, 0) : s5ctx_get_conductor_entry(s5ctx, 1); 

		rc = s5socket_connect(clt_socket, 
							  conductor_0_entry ? conductor_0_entry->front_ip : NULL, 
							  conductor_1_entry ? conductor_1_entry->front_ip : NULL, 
							 (uint16_t)(conductor_0_entry ? conductor_0_entry->front_port : 0),
							 (uint16_t)(conductor_1_entry ? conductor_1_entry->front_port : 0),
							  RCV_TYPE_MANUAL, CONNECT_TYPE_TEMPORARY);
	}
   	if(rc != 0)
    {   
       	S5LOG_AND_SET_ERROR("Failed to connect to any conductor");
       	goto socket_release;
    }

    s5_message_t *msg_reply = NULL;
    s5_message_t *msg = NULL;

    msg = s5msg_create(0);
    if (!msg)
    {   
        S5LOG_AND_SET_ERROR("Failed to create login requeset. Please check system memory available.");
        rc = -ENOMEM;
        goto socket_release;
    } 		
	
    msg->head.msg_type = MSG_TYPE_S5CLT_REQ;
    msg->head.listen_port = -1;
    msg->head.data_len = sizeof(s5_client_req_t);
    msg->data = (void*)req;
    msg->head.transaction_id = tid++;

    S5LOG_INFO("Send admin request(tid: %d)...\n", msg->head.transaction_id);
    msg_reply = s5socket_send_msg_wait_reply(clt_socket, msg);

    if(msg_reply)
    {
    	S5ASSERT(msg_reply->head.status == MSG_STATUS_OK);
	    S5LOG_INFO("Receive admin data reply(tid: %d)...\n", msg_reply->head.transaction_id);
        S5ASSERT(msg_reply->head.msg_type == MSG_TYPE_S5CLT_REPLY);
        S5ASSERT(msg_reply->data);
        *rpl = (s5_clt_reply_t*)(msg_reply->data);

        free(msg_reply);
        msg_reply = NULL;
    }
    else
    {
        S5LOG_AND_SET_ERROR("Failed to send requeset and get reply from conductor.");
        rc = -ECOMM;
    }
   
    free(msg);
    msg = NULL;	

socket_release:
    if (clt_socket)
    {
        s5socket_release(&clt_socket, SOCKET_TYPE_CLT);
    }
    return rc;
}

int s5ctx_send_request(const s5_context_t* s5ctx, s5_client_req_t* req, s5_clt_reply_t** rpl)
{
    int result = 0;
    result = send_request_to_conductor(s5ctx, req, rpl, FALSE);    
	if (*rpl == NULL)
		return -ECOMM;

	if((*rpl)->result == -EPERM) 
    {   
		free(*rpl);
  		return send_request_to_conductor(s5ctx, req, rpl, TRUE); 	        
    }  
	return result; 
}

int s5ctx_get_conductor_cnt(const s5_context_t* s5ctx)
{
	return s5ctx->conductor_list.count;
}

s5conductor_entry_t* s5ctx_get_conductor_entry(const s5_context_t* s5ctx, int ent_idx)
{
    s5_dlist_entry_t  *pos;
    int count;
    s5conductor_entry_t* conductor_ent = NULL;
    s5list_lock(&s5ctx->conductor_list);
    S5LIST_FOR_EACH(pos, count, (&s5ctx->conductor_list))
    {   
        conductor_ent = S5LIST_ENTRY(pos, s5conductor_entry_t, list);
        if (conductor_ent->index == ent_idx)
        {   
            break;
        }   
    }   
    s5list_unlock(&s5ctx->conductor_list);
    return conductor_ent;
}

