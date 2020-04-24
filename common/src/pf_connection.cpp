#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "pf_connection.h"
#include "pf_app_ctx.h"
#include "pf_message.h"

S5Connection::S5Connection():ref_count(0),state(0),on_destroy(NULL)
{
}

S5Connection::~S5Connection()
{
	cmd_pool.destroy();
	data_pool.destroy();
	reply_pool.destroy();
}

int S5Connection::close()
{
	if (__sync_val_compare_and_swap(&state, CONN_OK, CONN_CLOSED) != CONN_OK)
	{
		return 0;//connection already closed
	}

	S5LOG_INFO("close connection conn:%p, %s", this, connection_info.c_str());
	do_close();
	if(on_close)
		on_close(this);
	return 0;
}

int S5Connection::init_mempools()
{
	int rc = 0;
	if (io_depth <= 0 || io_depth > MAX_IO_DEPTH)
		return -EINVAL;
	rc = cmd_pool.init(sizeof(pf_message_head), io_depth * 2);
	if (rc)
		goto release1;
	rc = data_pool.init(MAX_IO_SIZE, io_depth * 2);
	if (rc)
		goto release2;
	rc = reply_pool.init(sizeof(pf_message_reply), io_depth * 2);
	if (rc)
		goto release3;
	return rc;
release3:
	data_pool.destroy();
release2:
	cmd_pool.destroy();
release1:
	return rc;
}

int parse_net_address(const char* ipv4, unsigned short port, /*out*/struct sockaddr_in* ipaddr)
{
	struct addrinfo *addr;
	int rc = getaddrinfo(ipv4, NULL, NULL, &addr);
	if (rc)
	{
		S5LOG_ERROR("Failed to getaddrinfo: %s, %s\n", ipv4, gai_strerror(rc));
		return -1;
	}
	*ipaddr = *(struct sockaddr_in*)addr->ai_addr;
	ipaddr->sin_port = htons(port);
	freeaddrinfo(addr);
	return rc;
}

int S5Connection::send_heartbeat()
{
	S5LOG_FATAL("send_heartbeat not implemented");
	return 0;
}
