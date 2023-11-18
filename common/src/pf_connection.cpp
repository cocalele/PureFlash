#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "pf_connection.h"
#include "pf_app_ctx.h"
#include "pf_message.h"

int PfConnection::total_count=0;
int PfConnection::closed_count=0;
int PfConnection::released_count=0;

PfConnection::PfConnection():ref_count(0),master(NULL), state(0), on_destroy(NULL)
{
	total_count++;
}

PfConnection::~PfConnection()
{
	released_count++;
}

int PfConnection::close()
{
	if (__sync_val_compare_and_swap(&state, CONN_OK, CONN_CLOSED) != CONN_OK)
	{
		return 0;//connection already closed
	}

	S5LOG_INFO("Close connection conn:%p, %s", this, connection_info.c_str());
	closed_count++;
	do_close();
	if(on_close)
		on_close(this);
	return 0;
}

int parse_net_address(const char* ipv4, unsigned short port, /*out*/struct sockaddr_in* ipaddr)
{
	struct addrinfo *addr;
	int rc = getaddrinfo(ipv4, NULL, NULL, &addr);
	if (rc)
	{
		S5LOG_ERROR("Failed to getaddrinfo: %s, %s", ipv4, gai_strerror(rc));
		return -1;
	}
	*ipaddr = *(struct sockaddr_in*)addr->ai_addr;
	ipaddr->sin_port = htons(port);
	freeaddrinfo(addr);
	return rc;
}

int PfConnection::send_heartbeat()
{
	S5LOG_FATAL("send_heartbeat not implemented");
	return 0;
}

#define C_NAME(x) case x: return #x;
const char* ConnState2Str(int conn_state)
{
	static __thread char buf[64];

	switch(conn_state){
		C_NAME(CONN_INIT)
		C_NAME(CONN_OK)
		C_NAME(CONN_CLOSED)
		C_NAME(CONN_CLOSING)
		default:
			sprintf(buf, "%d", conn_state);
			return buf;
	}
}