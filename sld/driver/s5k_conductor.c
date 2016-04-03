#include <net/tcp.h>
#include <linux/in.h>
#include "s5k_log.h"
#include "s5k_conductor.h"
#include "s5k_message.h"

static void cdkt_set_inetaddr(uint32_t sip, unsigned short port,
	struct sockaddr_in *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = sip;
	addr->sin_port = htons(port);
}

static uint32_t cdkt_inet_addr(const char *str)
{
	uint32_t a, b, c, d;
	char arr[4];
	sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
	arr[0] = a;
	arr[1] = b;
	arr[2] = c;
	arr[3] = d;
	return *(uint32_t *)arr;
}

/*
 * return value:
 * 0: success
 * minus num: msg reply error
 */
static int cdkt_parse_open_volume_reply(struct s5_imagectx* ictx, s5_message_t *msg_reply)
{
    s5_clt_reply_t* open_reply = (s5_clt_reply_t*)msg_reply->data;

    int ret = open_reply->result;
    if (ret != 0)
    {
        LOG_ERROR("Failed to get available toe info, status(%d).", ret);
        return ret;
    }

    ictx->volume_id = open_reply->reply_info.open_rpl_data->meta_data.volume_id;
    ictx->snap_seq = open_reply->reply_info.open_rpl_data->meta_data.snap_seq;
    ictx->volume_size = open_reply->reply_info.open_rpl_data->meta_data.volume_size;
    ictx->toe_ip = cdkt_inet_addr(open_reply->reply_info.open_rpl_data->nic_ip);
	ictx->toe_port =  open_reply->reply_info.open_rpl_data->nic_port;

    ictx->volume_ctx_id = open_reply->reply_info.open_rpl_data->volume_ctx_id;
//	ictx->iops_density = msg_reply->head.iops_density;

    LOG_INFO("Open_volume[%s], volume_id[%llu], volume_size[%llu], toe_ip[0x%x], "
		"toe_port[%d], volume_ctx_id[%d]",
    	ictx->dinfo.mode_conductor.volume_name, ictx->volume_id, ictx->volume_size,
    	ictx->toe_ip, ictx->toe_port, ictx->volume_ctx_id);
    return 0;
}

/* return value:
 * 0 : success
 * BDD_MSG_STATUS_CONNECT_CONDUCTOR_FAILED: connect conductor failed.
 * BDD_MSG_STATUS_CONDUCTOR_PARSE_MSG_FAILED: parse conductor message failed.
 * BDD_MSG_STATUS_BDD_NOMEM: no memory
 */
int cdkt_register_volume(struct s5_imagectx *ictx)
{
    int ret = 0;
    int rc = 0;
    int len = 0;
	int conductor_idx;
	struct device_info *dinfo = &ictx->dinfo;
	struct socket *socket_fd = NULL;
	struct sockaddr_in conductor_addr;
	struct msghdr tcp_rx_msghdr;
	struct msghdr tcp_tx_msghdr;
	uint32_t cdkt_ip = 0;
	uint32_t cdkt_port = 0;
    s5_message_t msg;
	s5_cltreq_volume_open_param_t *open_param = NULL;
#define SEND_IOV_SEGMENT 3
	struct kvec iov[SEND_IOV_SEGMENT];

    /* socket: create the socket */

    for (conductor_idx = 0; conductor_idx < MAX_CONDUCTOR_CNT; ++ conductor_idx)
    {
		cdkt_ip = dinfo->mode_conductor.conductor_list[conductor_idx].front_ip;
		cdkt_port = dinfo->mode_conductor.conductor_list[conductor_idx].front_port;

        if(cdkt_ip == 0)
            continue;

		cdkt_set_inetaddr(cdkt_ip, cdkt_port, &conductor_addr);

		if(socket_fd != NULL)
		{
			kernel_sock_shutdown(socket_fd, SHUT_RDWR);
			sock_release(socket_fd);
			socket_fd = NULL;
		}

		if ((ret = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP,
						&socket_fd)) < 0)
		{
			LOG_ERROR("Fail to create socket. errorno: %d.", ret);
			return ret;
		}
		else
		{
			tcp_tx_msghdr.msg_name = (struct sockaddr *) &conductor_addr;
			tcp_tx_msghdr.msg_namelen = sizeof(struct sockaddr);
			tcp_tx_msghdr.msg_control = NULL;
			tcp_tx_msghdr.msg_controllen = 0;
			tcp_tx_msghdr.msg_flags = MSG_WAITALL;

			tcp_rx_msghdr.msg_name = (struct sockaddr *) &conductor_addr;
			tcp_rx_msghdr.msg_namelen = sizeof(struct sockaddr);
			tcp_rx_msghdr.msg_control = NULL;
			tcp_rx_msghdr.msg_controllen = 0;
			tcp_rx_msghdr.msg_flags = MSG_WAITALL;
			LOG_INFO("Create socket success.");
		}

		ret = kernel_connect(socket_fd, (struct sockaddr *) &conductor_addr,
				sizeof(struct sockaddr_in), 0);
		if(ret < 0)
		{
			LOG_ERROR("Connect to server(0x%x:%d) Fail, ret(%d).", cdkt_ip,
					cdkt_port, ret);
			sock_release(socket_fd);
			socket_fd = NULL;
			continue;
		}
		else
		{
			LOG_INFO("Succeed to connect to conductor(0x%x:%d).", cdkt_ip,
					cdkt_port);
			break;
		}
    }

    if (conductor_idx == MAX_CONDUCTOR_CNT)
	{
		LOG_WARN("No conductor could be connected from open volume.");
        return BDD_MSG_STATUS_CONNECT_CONDUCTOR_FAILED;
	}

	msg.data = vzalloc(LBA_LENGTH);
	if(!msg.data)
	{
		LOG_ERROR("No memory when alloc msg data for open volume to conductor.");
		return -ENOMEM;
	}

	msg.head.magic_num = S5MESSAGE_MAGIC;
	msg.head.msg_type = MSG_TYPE_OPENIMAGE;
	msg.head.listen_port = (0x0000);
	msg.head.data_len = LBA_LENGTH;
	memset(msg.data, 0, LBA_LENGTH);

	open_param = (s5_cltreq_volume_open_param_t*)msg.data;

	open_param->nic_ip_blacklist_len = ictx->nic_ip_blacklist_len;
	memcpy(open_param->nic_ip_blacklist, ictx->nic_ip_blacklist,
		IPV4_ADDR_LEN * MAX_NIC_IP_BLACKLIST_LEN);

	open_param->volume_ctx_id = ictx->volume_ctx_id;

	memcpy(open_param->tenant_name, dinfo->mode_conductor.tenant_name, MAX_NAME_LEN);
	memcpy(open_param->volume_name, dinfo->mode_conductor.volume_name, MAX_NAME_LEN);
	memcpy(open_param->snap_name, dinfo->mode_conductor.snap_name, MAX_NAME_LEN);

	LOG_INFO("open tenant[%s], volume[%s] to conductor, before send msg.",
			dinfo->mode_conductor.tenant_name, dinfo->mode_conductor.volume_name);

	iov[0].iov_base = &msg.head;
	iov[0].iov_len = sizeof(s5_message_head_t);
	iov[1].iov_base = msg.data;
	iov[1].iov_len = LBA_LENGTH;
	iov[2].iov_base = &msg.tail;
	iov[2].iov_len = sizeof(s5_message_tail_t);

	len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;

	rc = kernel_sendmsg(socket_fd, &tcp_tx_msghdr, iov,
			SEND_IOV_SEGMENT, len);

	if(rc != len)
	{
		LOG_ERROR("Failed to send msg to conductor, ret(%d).", rc);
		ret = BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST;
		goto out;
	}

	//recv head
	rc = kernel_recvmsg(socket_fd, &tcp_rx_msghdr, &iov[0], 1,
			iov[0].iov_len, tcp_rx_msghdr.msg_flags);
	if (rc != sizeof(s5_message_head_t))
	{
		LOG_ERROR("Failed to receive msg head from conductor, ret(%d).", rc);
		ret = BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST;
		goto out;
	}
    S5ASSERT(msg.head.data_len > 0);

	//recv data
	memset(msg.data, 0, LBA_LENGTH);
	iov[1].iov_base = msg.data;
	iov[1].iov_len = msg.head.data_len;
	rc = kernel_recvmsg(socket_fd, &tcp_rx_msghdr, &iov[1], 1,
			iov[1].iov_len, tcp_rx_msghdr.msg_flags);
	if (rc != msg.head.data_len)
	{
		LOG_ERROR("Failed to receive msg data from conductor, ret(%d).", rc);
		ret = BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST;
		goto out;
	}

	//recv tail
	rc = kernel_recvmsg(socket_fd, &tcp_rx_msghdr, &iov[2], 1,
			iov[2].iov_len, tcp_rx_msghdr.msg_flags);
	if (rc != sizeof(s5_message_tail_t))
	{
		LOG_ERROR("Failed to receive msg tail from conductor, ret(%d).", rc);
		ret = BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST;
		goto out;
	}

	LOG_INFO("open tenant[%s], volume[%s] to conductor, after recv msg.",
			dinfo->mode_conductor.tenant_name,
			dinfo->mode_conductor.volume_name);

    if (msg.head.status == MSG_STATUS_OK)
    {
        ret = cdkt_parse_open_volume_reply(ictx, &msg);
        if (ret != 0)
        {
            ret = BDD_MSG_STATUS_CONDUCTOR_NO_AVAILABLE_NIC_FAILED;
            LOG_ERROR("No available toe info.");
        }
    }
    else
    {
        LOG_ERROR("open tenant[%s], volume[%s] to conductor, msg status(0x%x)",
				dinfo->mode_conductor.tenant_name,
				dinfo->mode_conductor.volume_name, msg.head.status);
        ret = BDD_MSG_STATUS_CONDUCTOR_PARSE_MSG_FAILED;//errno
    }

out:
	if(socket_fd != NULL)
	{
		kernel_sock_shutdown(socket_fd, SHUT_RDWR);
		sock_release(socket_fd);
		socket_fd = NULL;
	}
	vfree(msg.data);
    return ret;
}

/* return value:
 * 0 : success
 * BDD_MSG_STATUS_CONNECT_CONDUCTOR_FAILED: connect conductor failed.
 * BDD_MSG_STATUS_CONDUCTOR_PARSE_MSG_FAILED: parse conductor message failed.
 * BDD_MSG_STATUS_BDD_NOMEM: no memory
 */
int cdkt_unregister_volume(struct s5_imagectx *ictx)
{
    int ret = 0;
    int rc = 0;
    int len = 0;
	int conductor_idx;
	struct device_info *dinfo = &ictx->dinfo;
	struct socket *socket_fd = NULL;
	struct sockaddr_in conductor_addr;
	struct msghdr tcp_rx_msghdr;
	struct msghdr tcp_tx_msghdr;
	uint32_t cdkt_ip = 0;
	uint32_t cdkt_port = 0;
    s5_message_t msg;
	s5_cltreq_volume_close_param_t *close_param = NULL;
#define SEND_IOV_SEGMENT 3
	struct kvec iov[SEND_IOV_SEGMENT];

    for (conductor_idx = 0; conductor_idx < MAX_CONDUCTOR_CNT; ++ conductor_idx)
    {
		cdkt_ip = dinfo->mode_conductor.conductor_list[conductor_idx].front_ip;
		cdkt_port = dinfo->mode_conductor.conductor_list[conductor_idx].front_port;

        if(cdkt_ip == 0)
            continue;

		cdkt_set_inetaddr(cdkt_ip, cdkt_port, &conductor_addr);

		if(socket_fd != NULL)
		{
			kernel_sock_shutdown(socket_fd, SHUT_RDWR);
			sock_release(socket_fd);
			socket_fd = NULL;
		}

		if ((ret = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP,
						&socket_fd)) < 0)
		{
			LOG_ERROR("Fail to create socket. errorno: %d.", ret);
			return ret;
		}
		else
		{
			tcp_tx_msghdr.msg_name = (struct sockaddr *) &conductor_addr;
			tcp_tx_msghdr.msg_namelen = sizeof(struct sockaddr);
			tcp_tx_msghdr.msg_control = NULL;
			tcp_tx_msghdr.msg_controllen = 0;
			tcp_tx_msghdr.msg_flags = MSG_WAITALL;

			tcp_rx_msghdr.msg_name = (struct sockaddr *) &conductor_addr;
			tcp_rx_msghdr.msg_namelen = sizeof(struct sockaddr);
			tcp_rx_msghdr.msg_control = NULL;
			tcp_rx_msghdr.msg_controllen = 0;
			tcp_rx_msghdr.msg_flags = MSG_WAITALL;
			LOG_INFO("Create socket success.");
		}

		ret = kernel_connect(socket_fd, (struct sockaddr *) &conductor_addr,
				sizeof(struct sockaddr_in), 0);
		if(ret < 0)
		{
			LOG_ERROR("Connect to server(0x%x:%d) Fail, ret(%d).", cdkt_ip,
					cdkt_port, ret);
			sock_release(socket_fd);
			socket_fd = NULL;
			continue;
		}
		else
		{
			LOG_INFO("Succeed to connect to conductor(0x%x:%d).", cdkt_ip,
					cdkt_port);
			break;
		}
    }

    if (conductor_idx == MAX_CONDUCTOR_CNT)
	{
		LOG_WARN("No conductor could be connected for close volume.");
        return BDD_MSG_STATUS_CONNECT_CONDUCTOR_FAILED;
	}

	msg.data = vzalloc(LBA_LENGTH);
	if(!msg.data)
	{
		LOG_ERROR("No memory when alloc msg data for close volume to conductor.");
		return -ENOMEM;
	}

	msg.head.magic_num = S5MESSAGE_MAGIC;
	msg.head.msg_type = MSG_TYPE_CLOSEIMAGE;
	msg.head.data_len = LBA_LENGTH;
	msg.head.image_id = ictx->volume_id;
	msg.head.snap_seq = ictx->snap_seq;
	if (strlen(ictx->dinfo.mode_conductor.snap_name)!=0){
		msg.head.is_head = 0 ; //snapshot
	}else{
		msg.head.is_head = 1; //head
	}
	memset(msg.data, 0, LBA_LENGTH);

	close_param = (s5_cltreq_volume_close_param_t*)msg.data;

	close_param->volume_ctx_id = ictx->volume_ctx_id;
	memcpy(close_param->tenant_name, ictx->dinfo.mode_conductor.tenant_name, MAX_NAME_LEN);

	LOG_INFO("close tenant[%s], volume[%s] to conductor, before send msg.",
			ictx->dinfo.mode_conductor.tenant_name,
			ictx->dinfo.mode_conductor.volume_name);

	iov[0].iov_base = &msg.head;
	iov[0].iov_len = sizeof(s5_message_head_t);
	iov[1].iov_base = msg.data;
	iov[1].iov_len = LBA_LENGTH;
	iov[2].iov_base = &msg.tail;
	iov[2].iov_len = sizeof(s5_message_tail_t);

	len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;

	rc = kernel_sendmsg(socket_fd, &tcp_tx_msghdr, iov,
			SEND_IOV_SEGMENT, len);

	if(rc != len)
	{
		LOG_ERROR("Failed to send close volume msg to conductor, ret(%d).", rc);
		ret = BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST;
		goto out;
	}

	//recv close volume msg
    iov[0].iov_base = &msg.head;
    iov[0].iov_len = sizeof(s5_message_head_t);
    iov[1].iov_base = &msg.tail;
    iov[1].iov_len = sizeof(s5_message_tail_t);
	len = iov[0].iov_len + iov[1].iov_len;
	//recv head
	rc = kernel_recvmsg(socket_fd, &tcp_rx_msghdr, iov, 2,
			len, tcp_rx_msghdr.msg_flags);
	if (rc != len)
	{
		LOG_ERROR("Failed to receive close volume msg from conductor, ret(%d).", rc);
		ret = BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST;
		goto out;
	}

	LOG_INFO("Close tenant[%s], volume[%s] to conductor, after recv msg.",
			dinfo->mode_conductor.tenant_name,
			dinfo->mode_conductor.volume_name);

	LOG_INFO("Close volume reply-msg type:%d nlba:%d status:%d.",
			 msg.head.msg_type, msg.head.nlba, msg.head.status);
    if (msg.head.status == MSG_STATUS_OK)
        ret = 0;
    else
        ret = BDD_MSG_STATUS_CONDUCTOR_PARSE_MSG_FAILED;//errno

out:
	if(socket_fd != NULL)
	{
		kernel_sock_shutdown(socket_fd, SHUT_RDWR);
		sock_release(socket_fd);
		socket_fd = NULL;
	}
	vfree(msg.data);
    return ret;
}

#if 0
/* return value:
 * 0 : success
 * -1: connect conductor failed.
 * -2: parse conductor message failed.
 * -ENOMEM : no memory
 */
static int32_t bdd_get_ioctl_param (struct device_info* dinfo, struct ioctlparam *ictlarg)
{
    int32_t ret = 0;

    if (dinfo->toe_src.debug_mode == 0)
    {
        ret = register_volume_to_conductor(dinfo, ictlarg);
    }
    else
    {
		ictlarg->toe_ip = dinfo->toe_ip;
        ictlarg->toe_port = dinfo->toe_port;
        ictlarg->volume_id = dinfo->volume_id;
        ictlarg->volume_size = dinfo->volume_size;
    }

    strncpy(ictlarg->dev_name, dinfo->dev_name, MAX_DEVICE_NAME_LEN - 1);

    LOG_INFO("toe_ip(0x%x)-toe_port(%d)-dev_name(%s)-volume_id(%lu)-snap_seq(%d)"
		"-volume_size(%lu)-ret(%d)",
        ictlarg->toe_ip, ictlarg->toe_port, ictlarg->dev_name, ictlarg->volume_id,
        ictlarg->snap_seq, ictlarg->volume_size, ret);

    return ret;
}
#endif
