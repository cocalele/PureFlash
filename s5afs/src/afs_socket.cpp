#include "s5socket_impl.h"

#include <errno.h>

void* s5socket_create(s5_socket_type_t type, unsigned short local_port, const char* local_address)
{
	void*	ret = NULL;
	if(type == SOCKET_TYPE_CLT)
	{
		S5TCPCltSocket *sock = NULL;
		sock = new S5TCPCltSocket();
		if(!sock)
		{
			S5LOG_ERROR("Failed to s5socket_create:: can not new clt_sock!");
			goto FINALLY;
		}
		ret = sock;
	}
	else if(type == SOCKET_TYPE_SRV)
	{
		S5TCPServerSocket *sock = NULL;
		int rc = -1;
		sock = new S5TCPServerSocket();
		if(!sock)
		{
			S5LOG_ERROR("Failed to s5socket_create:: can not new srv_sock!");
			goto FINALLY;
		}	
		rc = sock->initServer(local_address, local_port);
		if(rc < 0)
		{
			if (local_address)
			{
				S5LOG_ERROR("Failed to s5socket_create:: srv_sock can not bind port [%s:%d].", local_address, local_port);
			}
			else
			{
				S5LOG_ERROR("Failed to s5socket_create:: srv_sock can not bind port [%d].", local_port);
			}
			delete sock;
			sock = NULL;
			goto FINALLY;
		}
		ret = sock;
	}
	else
	{
		S5LOG_ERROR("Failed to s5socket_create:: type(%d) is invalid.", type);
	}

FINALLY:
	return ret;
		
}

int s5socket_release(void** socket, s5_socket_type_t type)
{
	int rc = 0;
	if(socket)
	{
	if(type == SOCKET_TYPE_CLT)
	{
		delete *((S5TCPCltSocket **)socket);
		*socket = NULL;
	}
	else if(type == SOCKET_TYPE_SRV)
	{
		delete *((S5TCPServerSocket **)socket);
		*socket = NULL;
	}
	else
	{
		S5ASSERT(0 == "UNKNOWN SOCKET TYPE.");
		rc = -EINVAL;
	}
	}
	

	return rc;
}

int s5socket_connect(PS5CLTSOCKET socket, const char* foreignAddress0, const char* foreignAddress1, 
					 unsigned short foreignPort0, unsigned short foreignPort1,
					 s5_rcv_type_t autorcv, s5_connect_type_t type)
{	
	int rc = 0;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_connect:: param is invalid clt:%p.", sock);
		return -EINVAL;
	}

	if(foreignAddress0 == NULL && foreignAddress1 == NULL)
	{
		S5LOG_ERROR("Failed to s5socket_connect:: input ips are NULL.");
		return -EINVAL;
	}

	if(foreignAddress0 != NULL && strlen(foreignAddress0) != 0) 
	{
		rc = sock->connect(foreignAddress0, foreignPort0, autorcv, type);
		if(rc != 0)
		{
			S5LOG_WARN("Failed to s5socket_connect:: failed to connect ip: %s  rc:%d.", foreignAddress0, rc);
		}
		else
		{
			return rc;
		}
	}
	
	if(foreignAddress1 != NULL && strlen(foreignAddress1) != 0)
	{
		if(rc == -EINPROGRESS)
		{
			sock->resetFd();
		}

       	rc = sock->connect(foreignAddress1, foreignPort1, autorcv, type);
    	return rc;
	} 
	else
	{
		return rc;	
	}
}

PS5CLTSOCKET s5socket_accept(PS5SRVSOCKET socket, int autorcv)
{
	S5TCPServerSocket *sock = (S5TCPServerSocket *)socket;
	S5TCPCltSocket *connect = NULL;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_accept:: param is invalid srv:%p.", sock);
	}
	else
	{
		connect = sock->accept((s5_rcv_type_t)autorcv);
	}

	return (PS5CLTSOCKET)connect;
}

int s5socket_register_handle(void* socket, s5_socket_type_t type, 
			msg_type_t msg_type, recv_msg_handle recv_handle, void* recv_param)
{	
	int rc = -EINVAL;
	if(type == SOCKET_TYPE_CLT)
	{
		S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
		if(!sock)
		{
			S5LOG_ERROR("Failed to s5socket_register_handle:: param is invalid clt:%p.", sock);
			goto FINALLY;
		}
		rc = sock->register_recv_handle(msg_type, recv_handle, recv_param);
	}
	else if(type == SOCKET_TYPE_SRV)
	{
		S5TCPServerSocket *sock = (S5TCPServerSocket *)socket;
		if(!sock)
		{
			S5LOG_ERROR("Failed to s5socket_register_handle:: param is invalid srv:%p.", sock);
			goto FINALLY;
		}
		rc = sock->register_recv_handle(msg_type, recv_handle, recv_param);
	}
	else
	{
		S5LOG_ERROR("Failed to s5socket_register_handle:: type(%d) is invalid.", type);
	}
FINALLY:
	return rc;
}

int s5socket_unreigster_handle(void* socket, s5_socket_type_t type,
			msg_type_t msg_type)
{	
	int rc = -EINVAL;
	if(type == SOCKET_TYPE_CLT)
	{
		S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
		if(!sock)
		{
			S5LOG_ERROR("Failed to s5socket_unreigster_handle:: param is invalid clt:%p.", sock);
			goto FINALLY;
		}
		rc = sock->unregister_recv_handle(msg_type);
	}
	else if(type == SOCKET_TYPE_SRV)
	{
		S5TCPServerSocket *sock = (S5TCPServerSocket *)socket;
		if(!sock)
		{
			S5LOG_ERROR("Failed to s5socket_unreigster_handle:: param is invalid srv:%p.", sock);
			goto FINALLY;
		}
		rc = sock->unregister_recv_handle(msg_type);
	}
	else
	{
		S5LOG_ERROR("Failed to s5socket_unreigster_handle:: type(%d) is invalid.", type);
	}
FINALLY:

	return rc;
}

int s5socket_send_msg(PS5CLTSOCKET socket, s5_message_t *msg)
{	
	int rc = -EINVAL;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_send_msg:: param is invalid clt:%p.", sock);
		goto FINALLY;
	}
	rc = sock->send_msg(msg);

FINALLY:	
	return rc;
}


s5_message_t* s5socket_send_msg_wait_reply(PS5CLTSOCKET socket, s5_message_t *msg)
{	
	s5_message_t *reply_msg = NULL;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_send_msg_wait_reply:: param is invalid clt:%p.", sock);
		goto FINALLY;
	}
	reply_msg = sock->send_msg_wait_reply(msg);
	
FINALLY:	

	return reply_msg;
}

char* s5socket_get_foreign_ip(PS5CLTSOCKET socket)
{
	char* ip = NULL;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_get_foreign_ip:: param is invalid clt:%p.", sock);
	}
	else
		ip = sock->getForeignAddress();
	
	return ip;
}

int s5socket_get_foreign_port(PS5CLTSOCKET socket)
{
	int port = -1;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_get_foreign_ip:: socket is NULL.");
		return -EINVAL;
	}
	else
		port = sock->getForeignPort();
	
	return port;
}

int s5socket_set_user_data(PS5CLTSOCKET socket, void* user_data)
{
	int rc = -1;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_set_user_data socket is NULL.");
		return -EINVAL;
	}

	sock->user_data = user_data;
	rc = 0;
	return rc;
}
void* s5socket_get_user_data(PS5CLTSOCKET socket)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_get_user_data param is invalid. sock[%p]", sock);
		return NULL;
	}
	return sock->user_data;
}

int s5socket_register_conn_exception_handle(PS5CLTSOCKET socket, s5connection_exception_handle exc_handle, void* handle_param)
{
	int rc = 0;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock || !exc_handle)
	{
		S5LOG_ERROR("Failed to s5socket_register_conn_exception_handle param is invalid. sock[%p] exc_handle[%p] conncet_type is tempory?"
			, sock, exc_handle);
		return -EINVAL;
	}
	
	sock->exc_handle = exc_handle;
	sock->exc_handle_param = handle_param;
	return rc;
}

int s5socket_unregister_conn_exception_handle(PS5CLTSOCKET socket)
{
	int rc = 0;
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	if(!sock)
	{
		S5LOG_ERROR("Failed to s5socket_unregister_conn_exception_handle param is invalid. sock[%p]", sock);
		return -EINVAL;
	}
	
	sock->exc_handle = NULL;
	sock->exc_handle_param = NULL;
	return rc;

}

int s5socket_create_reaper_thread(pthread_t* reap_socket_thread)
{
	return pthread_create(reap_socket_thread, NULL, reaper_handle_thread, NULL);  	
}

BOOL s5socket_get_handler_init_flag(PS5CLTSOCKET socket)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	return sock->handler_init;
}

void s5socket_set_handler_init_flag(PS5CLTSOCKET socket, BOOL init)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	sock->handler_init = init;
}

void s5socket_lock_handler_mutex(PS5CLTSOCKET socket)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	pthread_mutex_lock(&(sock->handler_mutex));
}

void s5socket_unlock_handler_mutex(PS5CLTSOCKET socket)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	pthread_mutex_unlock(&(sock->handler_mutex));
}

void s5socket_wait_cond(PS5CLTSOCKET socket)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	pthread_cond_wait(&(sock->handler_cond), &(sock->handler_mutex));
}

void s5socket_signal_cond(PS5CLTSOCKET socket)
{
	S5TCPCltSocket *sock = (S5TCPCltSocket *)socket;
	pthread_cond_signal(&(sock->handler_cond));
}

