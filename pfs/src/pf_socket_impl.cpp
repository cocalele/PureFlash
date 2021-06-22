/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include "s5socket_impl.h"
#include "s5list.h"
#include "pf_log.h"

#include <netinet/tcp.h>

#include <sys/ioctl.h>
#include <sys/socket.h> 	 // For socket(), connect(), send(), and recv()
#include <sys/time.h>

#include <netdb.h>			 // For gethostbyname()
#include <unistd.h> 		 // For close()

#include <cstdlib>            // For atoi()
#include <errno.h>

#define MAXSLEEP 64

typedef void raw_type;       // Type used for raw data on this platform

int s5recv_keepalive_reply(void *sockParam, pf_message_t *msg, void *rcvparam);
int s5recv_keepalive(void *sockParam, pf_message_t *msg, void *rcvparam);

static int safe_snd(int fd, const void *buf, size_t count);
static int safe_rcv(int fd, void *buf, size_t count);

// Function to fill in address structure given an address and port
static void fillAddr(const char *address, unsigned short port,
					 sockaddr_in &addr)
{
	memset(&addr, 0, sizeof(addr));  // Zero out address structure
	addr.sin_family = AF_INET;       // Internet address
	addr.sin_addr.s_addr = inet_addr(address);
	addr.sin_port = htons(port);     // Assign port in network byte order
}

static void set_socket_bufsize(int sockDesc, int bufsize)
{
    int sock_bufsize = 0; 
    size_t size = sizeof(int);
    int rc = getsockopt(sockDesc, SOL_SOCKET, SO_RCVBUF, (char *)&sock_bufsize, (socklen_t *)&size);
    if(rc < 0) 
        S5LOG_TRACE("Failed to get rcv_buf_size when did getsockopt,  fd(%d) errnor(%d).", sockDesc, errno);
    rc = getsockopt(sockDesc, SOL_SOCKET, SO_SNDBUF, (char *)&sock_bufsize, (socklen_t *)&size);
    if(rc < 0) 
        S5LOG_TRACE("Failed to get snd_buf_size when did getsockipt, fd(%d) errnor(%d).", sockDesc, errno);
    sock_bufsize = bufsize;
    rc = setsockopt(sockDesc, SOL_SOCKET, SO_RCVBUF, (char *)&sock_bufsize, (int)sizeof(sock_bufsize));
    if(rc < 0) 
        S5LOG_TRACE("Failed to set rcv_buf_size(%d) when did setsockopt, fd(%d) errnor(%d).", sock_bufsize, sockDesc, errno);
    
    rc = getsockopt(sockDesc, SOL_SOCKET, SO_RCVBUF, (char *)&sock_bufsize, (socklen_t *)&size);
    if(rc < 0) 
        S5LOG_TRACE("Failed to get rcv_buf_size when did getsockopt, fd(%d) errnor(%d)", sockDesc, errno);
    
    sock_bufsize = bufsize;
    rc = setsockopt(sockDesc, SOL_SOCKET, SO_SNDBUF, (char *)&sock_bufsize, (int)sizeof(sock_bufsize));
    if(rc < 0) 
        S5LOG_TRACE("Failed to set snd_buf_size(%d) when did setsockopt,  fd(%d) errnor(%d)", sock_bufsize, sockDesc, errno);
    
    rc = getsockopt(sockDesc, SOL_SOCKET, SO_SNDBUF, (char *)&sock_bufsize, (socklen_t *) &size);
    if(rc < 0) 
        S5LOG_TRACE("Failed to get snd_buf_size getsockopt,  fd(%d) errnor(%d)", sockDesc, errno);
}

// Socket Code
Socket::Socket(int _type, int _protocol)
{
	type = _type;
	protocol = _protocol;

	// Make a new socket
	if ((sockDesc = socket(AF_INET, type, protocol)) < 0)
	{
		S5LOG_ERROR("Failed to create socket failed (socket()).");
	}
}

Socket::Socket(int sockDesc)
{
	this->sockDesc = sockDesc;
}

Socket::~Socket()
{
	if (sockDesc > 0)
	{
		closeSocketDesc();
	}
}

void Socket::closeSocketDesc()
{
	S5ASSERT(sockDesc > 0);
	shutdown(sockDesc, 2);
	::close(sockDesc);
	sockDesc = -1;
}

void Socket::resetFd()
{
	::close(sockDesc);
	if ((sockDesc = socket(AF_INET, type, protocol)) < 0)
	{
		S5LOG_ERROR("Failed to resetFd of socket creation failed (socket()%s).", strerror(errno));
	}
}

char *Socket::getLocalAddress()
{
	sockaddr_in addr;
	unsigned int addr_len = sizeof(addr);
	if (getsockname(sockDesc, (sockaddr *) &addr, (socklen_t *) &addr_len) < 0)
	{
		S5LOG_ERROR("Failed to fetch of local address failed (getsockname())%s.", strerror(errno));
		return NULL;
	}
	return inet_ntoa(addr.sin_addr);
}

int Socket::getLocalPort()
{
	sockaddr_in addr;
	unsigned int addr_len = sizeof(addr);

	if (getsockname(sockDesc, (sockaddr *) &addr, (socklen_t *) &addr_len) < 0)
	{
		S5LOG_ERROR("Failed to fetch of local port failed (getsockname()).");
		return -1;
	}
	return (int)ntohs(addr.sin_port);
}

int Socket::setLocalPort(unsigned short localPort)
{
	// Bind the socket to its port
	sockaddr_in localAddr;
	int rc = 0;
	memset(&localAddr, 0, sizeof(localAddr));
	localAddr.sin_family = AF_INET;
	localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	localAddr.sin_port = htons(localPort);
	rc = bind(sockDesc, (sockaddr *) &localAddr, sizeof(sockaddr_in));
	if (rc < 0)
	{
		S5LOG_TRACE("Failed to set local address and port(%d) failed (bind():%d) %s.", localPort, rc, strerror(errno));
	}

	return rc;
}

int Socket::setLocalAddressAndPort(const char *localAddress,
								   unsigned short localPort)
{
	// Get the address of the requested host
	sockaddr_in localAddr;
	int rc = 0;
	memset(&localAddr, 0, sizeof(localAddr));  // Zero out address structure
	localAddr.sin_family = AF_INET;       // Internet address
	localAddr.sin_port = htons(localPort);
	localAddr.sin_addr.s_addr = inet_addr(localAddress);

	//fillAddr(localAddress, localPort, localAddr);
	rc = bind(sockDesc, (sockaddr *) &localAddr, sizeof(sockaddr_in));
	if (rc < 0)
	{
		S5LOG_TRACE("Failed to set local address(%s) and port(%d) failed (bind():%d) %s.", localAddress, localPort, rc, strerror(errno));
	}
	return rc;
}

int Socket::register_recv_handle(msg_type_t type, recv_msg_handle recv_handle, void *recv_param)
{
    int rc = 0; 
    if(type < 0 || type >= MSG_TYPE_MAX)
    {    
        rc = -EINVAL;
        goto FINALLY;
    }    
    recv_handle_arr[type] = recv_handle;
    recv_handle_param[type] = recv_param;

FINALLY:
    return rc;
}

int Socket::unregister_recv_handle(msg_type_t type)
{
    int rc = 0; 
    if(type < 0 || type >= MSG_TYPE_MAX)
    {    
        rc = -EINVAL;
        goto FINALLY;
    }    
    recv_handle_arr[type] = NULL;
	recv_handle_param[type] = NULL;

FINALLY:
    return rc;
}

void *S5KeepAliveThreadMain(void *paramSock)
{
	pthread_t threadID;
	PfTCPCltSocket *clntSock = (PfTCPCltSocket *)paramSock;
	int retry = 0;
	threadID = pthread_self();
	clntSock->thread_keepalive = threadID;

	//// Guarantees that thread resources are deallocated upon return
	//pthread_detach(threadID);
	S5LOG_INFO("S5KeepAliveThreadMain thread(%llu) start.", (unsigned long long)threadID);
	pf_message_t *msg;
	msg = s5msg_create(0);
	msg->head.msg_type = MSG_TYPE_KEEPALIVE;
	msg->head.nlba = 0;
	msg->data = NULL;
	while(!clntSock->exitFlag && (clntSock->sockDesc != -1))
	{
		int rc = 0;

		//reconnect
		if(clntSock->con_invalid)
		{
			if(retry == 0)
			{
				clntSock->resetFd();
			}
			rc = clntSock->connect(clntSock->foreign_addr, clntSock->foreign_port,
								   clntSock->autorcv, clntSock->conntype);
			if(rc)
			{
				S5LOG_TRACE("S5KeepAliveThreadMain ReConnect failed rc(%d) retry(%d) %s.", rc, retry, strerror(errno));
				if(retry >= RETRY_CONN_TIMES)
				{
					if(clntSock->exc_handle)
					{
						S5LOG_TRACE("S5KeepAliveThreadMain can not reconnect to SERVER, call the exception-handle-callback.");
						clntSock->exc_handle(clntSock, clntSock->exc_handle_param);
					}
					else
					{
						S5LOG_TRACE("S5KeepAliveThreadMain can not reconnect to SERVER, the exception-handle-callback is NULL.");
					}
					break;
				}
				else
					retry++;
				sleep(RETRY_SLEEP_TIME);
				continue;
			}
			else
			{
				//reset retry.
				retry = 0;
				clntSock->con_invalid = 0;
			}
		}
		if(clntSock->autorcv == 0)
		{
			pf_message_t *msg_reply;
			msg_reply = clntSock->send_msg_wait_reply(msg);
			if(msg_reply)
			{
				clntSock->con_invalid = 0;//should check timeout?
			}
			else
			{
				S5LOG_TRACE("Failed to keep-alive,  err msg_reply(%p).", msg_reply);
				clntSock->con_invalid = 1;
			}
			s5msg_release_all(&msg_reply);
		}
		else
		{
			rc = clntSock->send_msg(msg);
			if(rc)
			{
				S5LOG_TRACE("Failed to keep-alive,  err send_msg(%d).", rc);
				clntSock->con_invalid = 1;
			}
		}
		if(clntSock->con_invalid)
		{
			S5LOG_TRACE("Failed to keep-alive,  find err and emit reconnecting.");
			if(clntSock->exitFlag)
			{
				S5LOG_TRACE("Need to exit thread.");
				break;
			}
			continue;//reconnect immediately.
		}
		sleep(1);
	}
	s5msg_release_all(&msg);
	S5LOG_INFO("S5KeepAliveThreadMain thread(%llu) exitFlag(%d) exit.", (unsigned long long)threadID, clntSock->exitFlag);
	return NULL;
}



// CommunicatingSocket Code
CommunicatingSocket::CommunicatingSocket(int type, int protocol)
	: Socket(type, protocol)
{
	pthread_mutex_init(&lock_recv, NULL);
	pthread_mutex_init(&lock_send, NULL);
	for(int i = 0; i < MSG_TYPE_MAX; i++)
	{
		recv_handle_arr[i] = NULL;
		recv_handle_param[i] = NULL;
	}
	set_socket_bufsize(sockDesc, 4*1024*1024);
	//int on=0;
	//rc = setsockopt(sockDesc,SOL_SOCKET,SO_LINGER,&on,sizeof(on));
	//struct timeval timeout = {3,0};
	//rc = setsockopt(sockDesc, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, (int)sizeof(struct timeval));
	//rc = getsockopt(sockDesc, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout,(socklen_t*) &size);
	//LOG_INFO("Socket[%d]::get snd_time_out:%d rc%d", sockDesc, (int)timeout.tv_sec, rc);
}

CommunicatingSocket::CommunicatingSocket(int newConnSD) : Socket(newConnSD)
{
	pthread_mutex_init(&lock_recv, NULL);
	pthread_mutex_init(&lock_send, NULL);
	for(int i = 0; i < MSG_TYPE_MAX; i++)
	{
		recv_handle_arr[i] = NULL;
		recv_handle_param[i] = NULL;
	}
	set_socket_bufsize(sockDesc, 4*1024*1024);

	//int on=0;
	//rc = setsockopt(sockDesc,SOL_SOCKET,SO_LINGER,&on,sizeof(on));
	//struct timeval timeout = {3,0};
	//rc = setsockopt(sockDesc, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, (int)sizeof(struct timeval));
	//rc = getsockopt(sockDesc, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout,(socklen_t*) &size);
	//LOG_INFO("Socket[%d]::get snd_time_out:%d rc%d", sockDesc, (int)timeout.tv_sec, rc);

}

int CommunicatingSocket::_connect(const char *foreignAddress,
								  unsigned short foreignPort)
{
	// Get the address of the requested host
	sockaddr_in destAddr;
	fillAddr(foreignAddress, foreignPort, destAddr);

	// Try to connect to the given port
	int error=-1, len;  
    len = sizeof(int);  
    timeval tm;  
    fd_set set;  
    unsigned long ul = 1;  
    ioctl(sockDesc, FIONBIO, &ul);  // set sockfd to non-block mode  
    int ret = 0;  
    if( connect(sockDesc, (struct sockaddr *)&destAddr, sizeof(destAddr)) == -1)  
    {  
        tm.tv_sec = 5;  
        tm.tv_usec = 0;  
        FD_ZERO(&set);  
        FD_SET(sockDesc, &set);  
        if( select(sockDesc + 1, NULL, &set, NULL, &tm) > 0)  
        {  
            getsockopt(sockDesc, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len);  
            if(error == 0) 
			{
				ret = 0;  
			}
            else 
			{
				ret = -1;  
			}
        } 
		else 
		{
			ret = -1;  
		}
    }  
    else 
	{
		ret = 0;  
	}

    ul = 0;  
    ioctl(sockDesc, FIONBIO, &ul); // set sockfd to block mode
    if(ret < 0)  
    {  
		//throw SocketException("Connect failed (connect())", true);
		S5LOG_TRACE("Connect failed (_connect()) errno:%d rc = %s.", errno, strerror(errno));
	}
	return ret;
}

char *CommunicatingSocket::getForeignAddress()
{
	sockaddr_in addr;
	unsigned int addr_len = sizeof(addr);
	int rc = getpeername(sockDesc, (sockaddr *) &addr, (socklen_t *) &addr_len);
	if (rc < 0)
	{
		S5LOG_TRACE("Fetch of foreign address failed (getpeername()) rc = %d %s.", rc, strerror(errno));
		return NULL;
	}
	return inet_ntoa(addr.sin_addr);
}

int CommunicatingSocket::getForeignPort()
{
	sockaddr_in addr;
	unsigned int addr_len = sizeof(addr);
	int rc = getpeername(sockDesc, (sockaddr *) &addr, (socklen_t *) &addr_len);

	if (rc < 0)
	{
		S5LOG_INFO("Fetch of foreign port failed (getpeername()) rc = %d %s", rc, strerror(errno));
		return rc;
	}
	return (int)ntohs(addr.sin_port);
}

int CommunicatingSocket::send_msg(pf_message_t *msg)
{
	int rc = 0;
	if(!msg)
	{
		S5LOG_ERROR("Failed to send_msg param is invalid.");
		return -EINVAL;
	}
	
	rc = send_msg_internal(msg);
	
	return rc;
}

pf_message_t *CommunicatingSocket::send_msg_wait_reply(const pf_message_t *msg)
{
	pf_message_t *reply_msg = NULL;
	if(!msg)
	{
		S5LOG_ERROR("Failed to send_msg_wait_reply param is invalid.");
		return NULL;
	}
	
	int rc = send_msg_internal(msg);
	if(rc)
	{
		S5LOG_ERROR("Failed to send_msg_wait_reply rc(%d)", rc);
	}
	reply_msg = recv_msg_internal();
	return reply_msg;
}

int CommunicatingSocket::send_msg_internal(const pf_message_t *msg)
{
	int rc = -1;
	if(!msg)
	{
		S5LOG_ERROR("Failed to send_msg_internal param is invalid.");
		rc = -EINVAL;
		return rc;
	}

	//mutex lock
	pthread_mutex_lock(&lock_send);
	
	rc = safe_snd(sockDesc, &(msg->head), sizeof(msg->head));
	if(rc < 0)
		goto FINAL;
	
	if(msg->data)
	{
		rc = safe_snd(sockDesc, msg->data, (size_t)msg->head.data_len);
		if(rc < 0)
			goto FINAL;
	}
	
	rc = safe_snd(sockDesc, &(msg->tail), sizeof(msg->tail));
	if(rc < 0)
		goto FINAL;

FINAL:
	//mutex unlock
	pthread_mutex_unlock(&lock_send);
	return rc;
}


pf_message_t  *CommunicatingSocket::recv_msg_internal()
{
	pf_message_t *reply_msg = NULL;
	size_t headlen, datalen, taillen;
	int rcvlen;

	//mutex lock
	pthread_mutex_lock(&lock_recv);
	reply_msg = (pf_message_t *)malloc(sizeof(pf_message_t));
	if(!reply_msg)
	{
		S5LOG_ERROR("Failed to recv_msg_internal: Can not malloc reply_msg.");
		goto  FINALLY;
	}
	memset(reply_msg, 0, sizeof(*reply_msg));
	
	//receive msg_head, then compute the length of data.
	headlen = sizeof(pf_message_head_t);
	rcvlen = safe_rcv(sockDesc, (raw_type *)&reply_msg->head, headlen);
	if((rcvlen < 0) || (rcvlen < (int)headlen))
	{
		//recv error or not entity.
		S5LOG_TRACE("Failed to recv_msg_internal: Received head failed (recv()) recv(%d) of len(%lu) errno:%d %s.", rcvlen, headlen, errno, strerror(errno));
		goto RELEASE_MSG;
	}

	//get the low 24bits as status,high 8bits reserve for ic.
	reply_msg->head.status = reply_msg->head.status & 0x00FFFFFF;

	datalen = (size_t)reply_msg->head.data_len;
	if(datalen > 0)
	{
		reply_msg->data = (char*)malloc(datalen);
		if(!reply_msg->data)
		{
			S5LOG_TRACE("Failed to recv_msg_internal: Can not malloc reply_msg->data, datalen:%lu.", datalen);
			goto  RELEASE_MSG_DATA;
		}
		rcvlen = safe_rcv(sockDesc, (raw_type *)reply_msg->data, datalen);
		if((rcvlen < 0) || (rcvlen < (int)datalen))
		{
			//recv error or not entity.
			S5LOG_TRACE("Failed to recv_msg_internal: Received data (recv()) recv(%d) of len(%lu) %s.", rcvlen, datalen, strerror(errno));
			goto RELEASE_MSG_DATA;
		}
	}

	taillen = sizeof(pf_message_tail_t);
	rcvlen = safe_rcv(sockDesc, (raw_type *)&reply_msg->tail, taillen);
	if((rcvlen < 0) || (rcvlen < (int)taillen))
	{
		//recv error or not entity.
		S5LOG_TRACE("Failed to received tail (recv()) recv(%d) of len(%lu) %s.", rcvlen, taillen, strerror(errno));
		goto RELEASE_MSG_DATA;
	}

	//mutex unlock
	pthread_mutex_unlock(&lock_recv);
	return reply_msg;

RELEASE_MSG_DATA:
	if(reply_msg && reply_msg->data)
	{
		free(reply_msg->data);
		reply_msg->data = NULL;
	}
RELEASE_MSG:
	if(reply_msg)
	{
		free(reply_msg);
		reply_msg = NULL;
	}
FINALLY:
	//mutex unlock
	pthread_mutex_unlock(&lock_recv);
	return reply_msg;
}


static int safe_snd(int fd, const void *buf, size_t count)
{
	int times = 0;
	while (count > 0)
	{
		ssize_t r = send(fd, (raw_type *)buf, count, MSG_NOSIGNAL);
		if (r <= 0)
		{
			pthread_t threadID = pthread_self();
			S5LOG_TRACE("Thread[%llu] SOCKET::safe_snd err rc:%ld  errno:%d fd:%d.", (unsigned long long)threadID, r, errno, fd);
			if (errno == EINTR)
				continue;
			if(errno == EWOULDBLOCK || errno == EAGAIN)
			{
				S5LOG_TRACE("Thread[%llu] SOCKET::safe_snd TIMEOUT or EAGAIN rc:%ld	errno:%d fd:%d retry times[%d].", (unsigned long long)threadID, r, errno, fd, times);
				if(times < RETRY_CONN_TIMES)
				{
					times++;
					continue;
				}
				else
				{
					break;
				}
			}

			return -errno;
		}
		count -= (size_t)r;
		buf = (char *)buf + r;
	}
	return 0;
}


static int safe_rcv(int fd, void *buf, size_t count)
{
	int cnt = 0;
	while (cnt < (int)count)
	{
		ssize_t r = recv(fd, (raw_type *)buf, (size_t)(count - (size_t)cnt), MSG_WAITALL);
		if (r <= 0)
		{
			if (r == 0)
			{
				// EOF
				return cnt;
			}
			S5LOG_TRACE("SOCKET::safe_rcv err rc:%ld  errno:%d fd:%d.", r, errno, fd);
			//if (errno == EINTR) //Can't stop by Ctrl-C when socket not response
			//	continue;
			return -errno;
		}
		cnt += (int)r;
		buf = (char *)buf + r;
	}
	return cnt;
}

// TCP client handling function
static int HandleTCPClient(void *paramSock)
{
	PfTCPCltSocket *sock = (PfTCPCltSocket *)paramSock;
	int retry = 0;
	while(!sock->exitFlag && (sock->sockDesc != -1))
	{
		//recv msg.
		pf_message_t *reply_msg = NULL;
		reply_msg = sock->recv_msg_internal();
		if(reply_msg)
		{
			msg_type_t type = (msg_type_t)reply_msg->head.msg_type;
			if(type > MSG_TYPE_MAX || type < 0)
			{
				S5LOG_TRACE("Recv msg error msg_type:%d.", type);
				s5msg_release_all(&reply_msg);
			}
			else
			{
				if(sock->recv_handle_arr[type])
					sock->recv_handle_arr[type](sock, reply_msg, sock->recv_handle_param[type]);
				else
				{
					S5LOG_WARN("Unregister handle for msg_type:%d.", reply_msg->head.msg_type);
					s5msg_release_all(&reply_msg);
				}
				retry = 0;
			}
		}
		else
		{
			if(sock->exitFlag)
			{
				S5LOG_INFO("Find socket had been deleted, exit thread immediately.");
				break;
			}

			if(sock->conntype == CONNECT_TYPE_STABLE)
			{
				if(retry >= RETRY_CONN_TIMES)
				{
					S5LOG_TRACE("Failed to receive from client retry[%d], please check client socket, exit thread immediately.", retry);
					if(sock->exc_handle)
					{    
						sock->exc_handle(sock, sock->exc_handle_param);
					} 
					break;
				}
				sleep(RETRY_SLEEP_TIME);
				retry++;
			}
			else
			{
				S5LOG_INFO("Recv msg error,break recv-thread for CONNECT_TYPE_TEMPORARY");
			
				if(sock->exc_handle)
				{
					sock->exc_handle(sock, sock->exc_handle_param);
				}
				break;
			}
		}
	}

	S5LOG_INFO("HandleTCPClient thread exitFlag:%d sockDesc:%d.", sock->exitFlag, sock->sockDesc);
	return 0;
}

void *S5RcvThreadMain(void *paramSock)
{
	pthread_t threadID;
	PfTCPCltSocket *clntSock = (PfTCPCltSocket *)paramSock;
	threadID = pthread_self();
	S5LOG_INFO("S5RcvThreadMain thread(%llu) start.", (unsigned long long)threadID);

	//// Guarantees that thread resources are deallocated upon return

	// Extract socket file descriptor from argument
	HandleTCPClient(clntSock);

	pf_reap_thread_entry_t* thread_entry = NULL;
	
	thread_entry = (pf_reap_thread_entry_t*)malloc(sizeof(pf_reap_thread_entry_t));

	thread_entry->thread_list_entry.head = NULL;
	thread_entry->thread_id = threadID;
	s5list_lock(&PfTCPCltSocket::reap_thread_list_head);
	s5list_push_ulc(&thread_entry->thread_list_entry, &PfTCPCltSocket::reap_thread_list_head);
	s5list_signal_entry(&PfTCPCltSocket::reap_thread_list_head);
	s5list_unlock(&PfTCPCltSocket::reap_thread_list_head);

	/** to make sure the send thread unlock send_mutex*/
	pthread_mutex_lock(&(clntSock->lock_send));
	pthread_mutex_unlock(&(clntSock->lock_send));

	delete (PfTCPCltSocket *) clntSock;
	S5LOG_INFO("S5RcvThreadMain thread(%llu) exit.", (unsigned long long)threadID);
	
	return NULL;
}

void *S5CltRcvThreadMain(void *paramSock)
{
	PfTCPCltSocket *clntSock = (PfTCPCltSocket *)paramSock;
	pthread_t threadID = pthread_self();
	S5LOG_INFO("S5CltRcvThreadMain thread(%llu) start.", (unsigned long long)threadID);

	// Guarantees that thread resources are deallocated upon return
	pthread_detach(threadID);
	HandleTCPClient(clntSock);
	S5LOG_INFO("S5CltRcvThreadMain thread(%llu) exit.", (unsigned long long)threadID);
	return NULL;
}

pf_dlist_head_t  PfTCPCltSocket::reap_thread_list_head;

// PfTCPCltSocket Code
PfTCPCltSocket::PfTCPCltSocket()
	: CommunicatingSocket(SOCK_STREAM,
						  IPPROTO_TCP)
{
	autorcv = RCV_TYPE_MANUAL;
	conntype = CONNECT_TYPE_TEMPORARY;
	con_invalid = 1;
	exitFlag = 0;
	memset(foreign_addr, 0, sizeof(foreign_addr));
	foreign_port = 0;
	user_data = NULL;
	exc_handle = NULL;
	exc_handle_param = NULL;
	thread_recv = 0;
	thread_recv_clt = 0;
	thread_keepalive = 0;
	initMutexCond();
}

void PfTCPCltSocket::initMutexCond()
{
    handler_init = FALSE;
    pthread_mutex_init(&handler_mutex, NULL);
    pthread_cond_init(&handler_cond, NULL);
}

void PfTCPCltSocket::destroyMutexCond()
{
    pthread_mutex_destroy(&handler_mutex);
    pthread_cond_destroy(&handler_cond);
}

PfTCPCltSocket::~PfTCPCltSocket()
{
	exitFlag = 1;
	con_invalid = 1;
	foreign_port = 0;
	user_data = NULL;
	destroyMutexCond();
	if(thread_keepalive)
	{
		pf_wait_thread_end(thread_keepalive);
	}
}

void *reaper_handle_thread(void *param)
{
	pf_dlist_entry_t *entry = NULL;
	int need_handle = 0;

	while(true)
	{
		pf_reap_thread_entry_t* socketThread = NULL;
		
		//get msg from reap_thread_list_head.
		s5list_lock(&PfTCPCltSocket::reap_thread_list_head);
		need_handle = PfTCPCltSocket::reap_thread_list_head.count;
		if(need_handle == 0)
		{
			s5list_wait_entry(&PfTCPCltSocket::reap_thread_list_head);
		}
		
        entry = s5list_poptail_ulc(&PfTCPCltSocket::reap_thread_list_head);

		if(!entry)
		{
			s5list_unlock(&PfTCPCltSocket::reap_thread_list_head);
			S5LOG_TRACE("Failed to get valid pthread_t threade_list_head.");
			continue;
		}	

		socketThread = S5LIST_ENTRY(entry, pf_reap_thread_entry_t, thread_list_entry);
	    s5list_unlock(&PfTCPCltSocket::reap_thread_list_head);	
	
		pf_wait_thread_end(socketThread->thread_id);
		free(entry);	
	}
	
	return NULL;
}

PfTCPCltSocket::PfTCPCltSocket(int newConnSD) : CommunicatingSocket(newConnSD)
{
	autorcv = RCV_TYPE_MANUAL;
	conntype = CONNECT_TYPE_TEMPORARY;
	con_invalid = 1;
	exitFlag = 0;
	memset(foreign_addr, 0, sizeof(foreign_addr));
	foreign_port = 0;
	user_data = NULL;
	exc_handle = NULL;
	exc_handle_param = NULL;
	thread_recv = 0;
	thread_keepalive = 0;
	initMutexCond();
}

int PfTCPCltSocket::connect(const char *foreignAddress,
						 unsigned short foreignPort, pf_rcv_type_t autorcv, pf_connect_type_t type)
{
	int rc = 0;
	int numsec;
	for (numsec = 1; numsec <= MAXSLEEP; numsec <<= 1)
	{
		rc = _connect(foreignAddress, foreignPort);
		if (rc == 0)
			break;
		if (numsec <= MAXSLEEP/2)
		{
			S5LOG_WARN("Retry fail to connect (connect()) rc(%d) errno(%d) %s.", rc, errno, strerror(errno));
			sleep(numsec);
			resetFd();
		}
	}
	if (rc < 0)
	{
		rc = -errno;
		if(errno != EISCONN)
		{
			//throw SocketException("Connect failed (connect())", true);
			S5LOG_ERROR("Failed to connect (connect()) rc(%d) errno(%d) %s.", rc, errno, strerror(errno));
		}
		else
		{
			S5LOG_TRACE("Failed to connect that had been connected, nothing to do.");
			return 0;
		}
	}
	else
	{
		this->autorcv = autorcv;
		this->conntype = type;
		this->con_invalid = 0;
		if(autorcv == RCV_TYPE_AUTO)
		{
			rc = pthread_create(&thread_recv_clt, NULL, S5CltRcvThreadMain, (void *) this);
			if(rc)
			{
				S5LOG_ERROR("Failed to create S5CltRcvThreadMain rc(%d) for socket(%d).", rc, sockDesc);
				S5ASSERT(0);
			}
			else
				S5LOG_INFO("Create S5CltRcvThreadMain OK for socket(%d) thread(%llu).", sockDesc, (unsigned long long)thread_recv_clt);
		}
		if(type == CONNECT_TYPE_STABLE)
		{
			//register keep-alive call-funtion.
			rc = this->register_recv_handle(MSG_TYPE_KEEPALIVE_REPLY, s5recv_keepalive_reply, NULL);
			S5ASSERT(rc == 0);
			safe_strcpy(foreign_addr, foreignAddress, sizeof(foreign_addr));
			foreign_port = foreignPort;

			//create keep-alive thread.
			rc = pthread_create(&thread_keepalive, NULL, S5KeepAliveThreadMain, (void *) this);
			if(rc)
			{
				S5LOG_ERROR("Failed to create S5KeepAliveThreadMain rc(%d) for socket(%d).", rc, sockDesc);
				S5ASSERT(0);
			}
			else
				S5LOG_INFO("Create S5KeepAliveThreadMain OK for socket(%d) thread(%llu).", sockDesc, (unsigned long long)thread_keepalive);
		}
	}

	return rc;
}

// PfTCPServerSocket Code
PfTCPServerSocket::PfTCPServerSocket()
	: Socket(SOCK_STREAM, IPPROTO_TCP)
{
	for(int i = 0; i < MSG_TYPE_MAX; i++)
	{
		recv_handle_arr[i] = NULL;
		recv_handle_param[i] = NULL;
	}
	int on = 1;
	int rc = setsockopt(sockDesc, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if(rc < 0)
    {   
	    S5LOG_ERROR("Failed to setsockopt PfTCPServerSocket: reuse failed.");
        exit(EXIT_FAILURE);
    }

	int flag = 1;
	setsockopt(sockDesc, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
}

int PfTCPServerSocket::initServer(const char *localAddress, unsigned short localPort, int queueLen)
{
	int rc = 0;
	if(!localAddress)
		rc = setLocalPort(localPort);
	else
		rc = setLocalAddressAndPort(localAddress, localPort);
	if(rc < 0)
		goto FINALLY;

	rc = setListen(queueLen);
	if(rc < 0)
		goto FINALLY;

FINALLY:
	return rc;
}

PfTCPCltSocket *PfTCPServerSocket::accept(pf_rcv_type_t autorcv)
{
	int newConnSD;
	PfTCPCltSocket *newsock = NULL;
	if ((newConnSD = ::accept(sockDesc, NULL, 0)) < 0)
	{
		S5LOG_ERROR("Failed to accept (accept()) rc = %d %s.", newConnSD, strerror(errno));
		return NULL;
	}
	else
	{
		//check the client is valid yes or no?
	}

	newsock = new PfTCPCltSocket(newConnSD);
	if(!newsock)
		goto FINALLY;

	//init register handle function.
	for(int i = 0; i < MSG_TYPE_MAX; i++)
	{
		newsock->recv_handle_arr[i] = recv_handle_arr[i];
		newsock->recv_handle_param[i] = recv_handle_param[i];
	}

	//register keep-alive call-funtion.
	newsock->register_recv_handle(MSG_TYPE_KEEPALIVE, s5recv_keepalive, NULL);
	if(autorcv == RCV_TYPE_AUTO)
	{
		int rc=0;
		for(int count = 0; count < RETRY_CONN_TIMES; count ++)
		{
			rc = pthread_create(&(newsock->thread_recv), NULL, S5RcvThreadMain, (void *) newsock);
			if(rc == 0)
			{
				S5LOG_INFO("S5RcvThreadMain create OK for socket(%d) thread(%llu).", newConnSD, (unsigned long long)newsock->thread_recv);
				newsock->autorcv = autorcv;
				return newsock;
			}
		}
		S5LOG_ERROR("Failed to create S5RcvThreadMain rc(%d).", rc);
		delete newsock;
		return NULL;
	}
FINALLY:
	return newsock;
}

int PfTCPServerSocket::setListen(int queueLen)
{
	int rc = 0;
	rc = listen(sockDesc, queueLen);
	if (rc < 0)
	{
		S5LOG_TRACE("Failed to listen socket (listen()) rc(%d) %s.", rc, strerror(errno));
	}
	return rc;
}


int s5recv_keepalive_reply(void *sockParam, pf_message_t *msg, void *param)
{
	int rc = 0;
	PfTCPCltSocket *socket = (PfTCPCltSocket *)sockParam;
	if(msg)
	{
		socket->con_invalid = 0;
		s5msg_release_all(&msg);
	}
	else
	{
		S5LOG_INFO("Failed to recv_msg_comm_reply  msg is NULL.");
		rc = -EINVAL;
		socket->con_invalid = 1;
	}
	return rc;
}

int s5recv_keepalive(void *sockParam, pf_message_t *msg, void *param)
{
	int rc = 0;
	PfTCPCltSocket *socket = (PfTCPCltSocket *)sockParam;
	if(msg)
	{
		//handle stastic-info
		socket->con_invalid = 0;
		socket->conntype = CONNECT_TYPE_STABLE;	//the connect-socket conntype is stable.
		pf_message_t *msg_keepalive_reply = s5msg_create(0);
		msg_keepalive_reply->head.msg_type = MSG_TYPE_KEEPALIVE_REPLY;
		rc = socket->send_msg(msg_keepalive_reply);
		if(rc)
			S5LOG_TRACE("Failed to send keepalive-reply rd(%d).", rc);
		s5msg_release_all(&msg_keepalive_reply);
		s5msg_release_all(&msg);
		rc = 0;
	}
	
	return rc;
}

void pf_wait_thread_end(pthread_t pid)
{
	int rc = 0;
	void *res;

	rc = pthread_join(pid, &res);
	if (rc != 0)
	{
		S5LOG_INFO("Thread(pid%llu): pthread_join rc:%d\n", (unsigned long long)pid, rc);
		if (res == PTHREAD_CANCELED)
			S5LOG_INFO("Thread (pid%llu): thread was canceled\n", (unsigned long long)pid);
	}
	else
		S5LOG_INFO("Terminated thread(pid(%llu) normally.\n", (unsigned long long)pid);
}


