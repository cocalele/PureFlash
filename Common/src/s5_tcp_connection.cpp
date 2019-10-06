#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "s5_tcp_connection.h"
#include "s5_utils.h"

int S5TcpConnection::init(int sock_fd, S5Poller* poller, int send_q_depth, int recv_q_depth)
{
	int rc = 0;
	this->socket_fd = sock_fd;
	this->poller = poller;
	int const1 = 1;
	rc = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&const1, sizeof(int));
	if (rc)
	{
		S5LOG_ERROR("set TCP_NODELAY failed!");
	}

	connection_info = get_socket_addr(sock_fd);
	rc = send_q.init("net_send_q", send_q_depth, TRUE);
	if (rc)
		goto release1;
	rc = recv_q.init("net_recv_q", recv_q_depth, TRUE);
	if (rc)
		goto release2;
	rc = poller->add_fd(send_q.event_fd, EPOLLIN | EPOLLET, on_send_q_event, this);
	if (rc)
		goto release3;
	rc = poller->add_fd(recv_q.event_fd, EPOLLIN | EPOLLET, on_recv_q_event, this);
	if (rc)
		goto release4;
	rc = poller->add_fd(sock_fd, EPOLLOUT | EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP  | EPOLLRDHUP, on_socket_event, this);
	if (rc)
		goto release5;
	return 0;
release5:
	poller->del_fd(recv_q.event_fd);
release4:
	poller->del_fd(send_q.event_fd);
release3:
	recv_q.destroy();
release2:
	send_q.destroy();
release1:
	poller = NULL;
	sock_fd = 0;
	S5LOG_ERROR("Failed init connection, rc:%d", rc);
	return rc;
}
void S5TcpConnection::on_send_q_event(int fd, uint32_t event, void* c)
{
	S5TcpConnection* conn = (S5TcpConnection*)c;
	if (conn->send_bd != NULL)
		return; //send in progress
	if(conn->send_bd == NULL)
	{
		S5Event evt;
		int rc = conn->send_q.get_event(&evt);
		if(rc != 0)
		{
			S5LOG_ERROR("Failed get event from send_q, rc:%d", rc);
			return;
		}
		conn->start_send((BufferDescriptor*)evt.arg_p);
	}
}
void S5TcpConnection::on_recv_q_event(int fd, uint32_t event, void* c)
{
	S5TcpConnection* conn = (S5TcpConnection*)c;
	if (conn->recv_bd != NULL)
		return;//receive in progress
	if (conn->recv_bd == NULL)
	{
		S5Event evt;
		int rc = conn->recv_q.get_event(&evt);
		if (rc != 0)
		{
			S5LOG_ERROR("Failed get event from recv_q, rc:%d", rc);
			return;
		}
		conn->start_recv((BufferDescriptor*)evt.arg_p);
	}
}
void S5TcpConnection::start_send(BufferDescriptor* bd)
{
	recv_bd = bd;
	recv_buf = bd->buf;
	wanted_recv_len = bd->data_len;
	recved_len = 0;
	do_receive();
}
void S5TcpConnection::start_recv(BufferDescriptor* bd)
{
	send_bd = bd;
	send_buf = bd->buf;
	wanted_send_len = bd->data_len;
	sent_len = 0;
	do_send();
}

void S5TcpConnection::on_socket_event(int fd, uint32_t events, void* c)
{
	S5TcpConnection* conn = (S5TcpConnection*)c;
	if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
	{
		if (events & EPOLLERR)
		{
			S5LOG_ERROR("TCP connection get EPOLLERR, %s", conn->connection_info.c_str());
		}
		else
		{
			S5LOG_INFO("TCP connection closed by peer, %s", conn->connection_info.c_str());
		}
		conn->close();
		return;
	}
	if (events & (EPOLLIN | EPOLLPRI))
	{
		conn->readable = TRUE;
	}
	if (events & (EPOLLOUT))
	{
		conn->writeable = TRUE;
	}

	if (conn->readable)
	{
		conn->do_receive();
	}
	if (conn->writeable)
	{
		conn->do_send();
	}
}

int S5TcpConnection::rcv_with_error_handle()
{
	while (recved_len < wanted_recv_len)
	{
		ssize_t rc = 0;
		rc = recv(socket_fd, (char*)recv_buf + recved_len,
			(size_t)(wanted_recv_len - recved_len), MSG_DONTWAIT);

		if (likely(rc > 0))
			recved_len += (int)rc;
		else if (rc == 0)
		{
			S5LOG_DEBUG("recv return rc:0");
			return -ECONNABORTED;
		}
		else
		{
			if (errno == EAGAIN)
			{
				readable = FALSE; //all data has readed, wait next
				return -EAGAIN;
			}
			else if (errno == EINTR)
			{
				S5LOG_DEBUG("Receive EINTR in rcv_with_error_handle");
				return -errno;
			}
			else if (errno == EFAULT)
			{
				S5LOG_FATAL("Recv function return EFAULT, for buffer addr:0x%p, rcv_len:%d",
					recv_buf, wanted_recv_len - recved_len);
			}
			else
			{
				S5LOG_ERROR("recv return rc:%d, %s need reconnect.", -errno, connection_info.c_str());
				readable = FALSE;
				need_reconnect = TRUE;
				return -errno;
			}
		}
	}

	return 0;
}

int S5TcpConnection::do_receive()
{
	int rc = 0;

	do {
		if (recv_bd == NULL)
			return 0;
		rc = rcv_with_error_handle();
		if (unlikely(rc != 0 && rc != -EAGAIN))
		{
			close();
			return -ECONNABORTED;
		}
		if (wanted_recv_len == recved_len)
		{
			rc = recv_bd->on_work_complete(recv_bd, this, NULL);
			if (unlikely(rc != 0))
			{
				S5LOG_WARN("on_recv_complete rc:%d", rc);
				if (rc == -ECONNABORTED)
				{
					close();
					return -ECONNABORTED;
				}
			}
		}
	} while (readable);
	return 0;

}

int S5TcpConnection::send_with_error_handle()
{
	if (wanted_send_len > sent_len)
	{
		ssize_t rc = send(socket_fd, (char*)send_buf + sent_len,
			wanted_send_len - sent_len, MSG_DONTWAIT);
		if (rc > 0)
		{
			sent_len += (int)rc;
		}
		else if (unlikely(rc == 0))
		{
			return -ECONNABORTED;
		}
		else
		{
			if (likely(errno == EAGAIN))
			{
				writeable = FALSE; //cann't send more, wait next
				return -errno;
			}
			else if (errno == EINTR)
			{
				S5LOG_DEBUG("Receive EINTR in send_with_error_handler");
				return -errno;
			}
			else if (errno == EFAULT)
			{
				S5LOG_FATAL("socket send return EFAULT");
			}
			else
			{
				S5LOG_ERROR("recv return rc:%d, %s need reconnect.", -errno, connection_info.c_str());
				writeable = FALSE;
				need_reconnect = TRUE;
				return -errno;
			}
		}
	}
	return 0;
}

int S5TcpConnection::do_send()
{
	int rc;
	do {
		if (send_bd == NULL)
			return 0;
		rc = send_with_error_handle();
		if (unlikely(rc != 0 && rc != -EAGAIN))
		{
			close();
			return -ECONNABORTED;
		}

		if (wanted_send_len == sent_len)
		{
			rc = send_bd->on_work_complete(send_bd, this, send_bd->cbk_data);
			if (unlikely(rc != 0 && rc != -EAGAIN))
			{
				S5LOG_DEBUG("Failed on_work_complete rc:%d", rc);
				close();
				return rc;
			}
		}
	} while (writeable);

	return 0;

}
