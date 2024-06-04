#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <exception>
#include <fcntl.h>
#include <pf_app_ctx.h>
#include <poll.h>

#include "pf_tcp_connection.h"
#include "pf_utils.h"
#include "pf_client_priv.h"
using namespace  std;
PfTcpConnection::PfTcpConnection(bool _is_client) :socket_fd(0), poller(NULL), recv_buf(NULL), recved_len(0), wanted_recv_len(0),
                                    recv_bd(NULL),send_buf(NULL), sent_len(0), wanted_send_len(0),send_bd(NULL),
                                    readable(FALSE), writeable(FALSE),need_reconnect(FALSE), is_client(_is_client)
                                    {}
PfTcpConnection::~PfTcpConnection()
{
	send_q.destroy();
	recv_q.destroy();
}
int PfTcpConnection::init(int sock_fd, PfPoller* poller, int send_q_depth, int recv_q_depth)
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

	connection_info = get_socket_desc(sock_fd, is_client);
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
	rc = poller->add_fd(sock_fd, EPOLLOUT | EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP  | EPOLLRDHUP | EPOLLET, on_socket_event, this);
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

int PfTcpConnection::do_close()
{
	S5LOG_INFO("Close connection conn:%p, %s", this, connection_info.c_str());
	poller->del_fd(send_q.event_fd);
	poller->del_fd(recv_q.event_fd);
	poller->del_fd(socket_fd);
	::close(socket_fd);

	if (pthread_self() == poller->tid)
		flush_wr();
	else
		poller->ctrl_queue.sync_invoke([this]()->int {
			this->flush_wr();
			return 0;
		});
	return 0;
}

void PfTcpConnection::flush_wr()
{
	if (recv_bd) {
		on_work_complete(recv_bd, WcStatus::WC_FLUSH_ERR, this, recv_bd->cbk_data);
		dec_ref();
		recv_bd = NULL;
	}

	if (send_bd) {
		on_work_complete(send_bd, WcStatus::WC_FLUSH_ERR, this, send_bd->cbk_data);
		dec_ref();
		send_bd = NULL;
	}

	PfFixedSizeQueue<S5Event>* q;
	int rc;
	if (!recv_q.is_empty()) {
		rc = recv_q.get_events(&q);
		if(rc == 0) {
			while(!q->is_empty()) {
				S5Event* t = &q->data[q->head];
				q->head = (q->head + 1) % q->queue_depth;
				BufferDescriptor* bd = (BufferDescriptor*)t->arg_p;
				on_work_complete(bd, WcStatus::WC_FLUSH_ERR, this, bd->cbk_data);
				dec_ref();
			}
		}
	}

	if (!send_q.is_empty()) {
		rc = send_q.get_events(&q);
		if(rc == 0) {
			while(!q->is_empty()) {
				S5Event* t = &q->data[q->head];
				q->head = (q->head + 1) % q->queue_depth;
				BufferDescriptor* bd = (BufferDescriptor*)t->arg_p;
				on_work_complete(bd, WcStatus::WC_FLUSH_ERR, this, bd->cbk_data);
				dec_ref();
			}
		}
	}
}

void PfTcpConnection::on_send_q_event(int fd, uint32_t event, void* c)
{
	//S5LOG_DEBUG("on_send_q_event called, event:%d", event);
	PfTcpConnection* conn = (PfTcpConnection*)c;
	if (conn->send_bd != NULL)
		return; //send in progress
	if(conn->send_bd == NULL && !conn->send_q.is_empty())
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
void PfTcpConnection::on_recv_q_event(int fd, uint32_t event, void* c)
{
	//S5LOG_DEBUG("on_recv_q_event called, event:%d", event);
	PfTcpConnection* conn = (PfTcpConnection*)c;
	if (conn->recv_bd != NULL)
		return;//receive in progress
	if (conn->recv_bd == NULL && !conn->recv_q.is_empty())
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

int PfTcpConnection::start_recv(BufferDescriptor* bd)
{
	bd->wr_op = WrOpcode::TCP_WR_RECV;
	recv_bd = bd;
	recv_buf = bd->buf;
	wanted_recv_len = bd->data_len;
	recved_len = 0;
	return do_receive();
}

int PfTcpConnection::start_recv(BufferDescriptor* bd, void* buf)
{
	bd->wr_op = WrOpcode::TCP_WR_RECV;
	recv_bd = bd;
	recv_buf = buf;
	wanted_recv_len = bd->data_len;
	recved_len = 0;
	return do_receive();
}

int PfTcpConnection::start_send(BufferDescriptor* bd)
{
	bd->wr_op = WrOpcode::TCP_WR_SEND;
	send_bd = bd;
	send_buf = bd->buf;
	wanted_send_len = bd->data_len;
	sent_len = 0;
	return do_send();
}

int PfTcpConnection::start_send(BufferDescriptor* bd, void* buf)
{
	bd->wr_op = WrOpcode::TCP_WR_SEND;
	send_bd = bd;
	send_buf = buf;
	wanted_send_len = bd->data_len;
	sent_len = 0;
	return do_send();
}

void PfTcpConnection::on_socket_event(int fd, uint32_t events, void* c)
{
	PfTcpConnection* conn = (PfTcpConnection*)c;
	
	//send_q, recv_q is valid after connection close
	//if(conn->state == CONN_CLOSED){
	//	S5LOG_WARN("connection:%p (%s) has been closed!", conn, conn->connection_info.c_str());
	//}
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

int PfTcpConnection::rcv_with_error_handle()
{
	while (recved_len < wanted_recv_len)
	{
		ssize_t rc = 0;
		rc = recv(socket_fd, (char*)recv_buf + recved_len,
			(size_t)(wanted_recv_len - recved_len), MSG_DONTWAIT);
		//S5LOG_DEBUG("recv %d bytes", rc);
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

int PfTcpConnection::do_receive()
{
	int rc = 0;

	do {
		if (recv_bd == NULL) {
			if (recv_q.is_empty()) {
				return 0;
			}
			S5Event evt;
			rc = recv_q.get_event(&evt);
			if (unlikely(rc)) {
				S5LOG_ERROR("Get event from recv_q failed");
			} else {
				BufferDescriptor* bd = (BufferDescriptor *) evt.arg_p;
				bd->wr_op = WrOpcode::TCP_WR_RECV;
				recv_bd = bd;
				recv_buf = bd->buf;
				wanted_recv_len = bd->data_len;
				recved_len = 0;

				//start_recv((BufferDescriptor *) evt.arg_p);
				continue;
			}
		}
		rc = rcv_with_error_handle();
		if (unlikely(rc != 0 && rc != -EAGAIN))
		{
			S5LOG_ERROR("TCP receive failed, rc:%d conn:%p (%s)", rc, this, connection_info.c_str());
			close();
			return -ECONNABORTED;
		}
		if (wanted_recv_len == recved_len)
		{
			BufferDescriptor* temp_bd = recv_bd;
			recv_bd = NULL;
			rc = on_work_complete(temp_bd, WC_SUCCESS, this, NULL);
			dec_ref();
			if (unlikely(rc < 0))
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

int PfTcpConnection::send_with_error_handle()
{
	if (wanted_send_len > sent_len)
	{
		ssize_t rc = send(socket_fd, (char*)send_buf + sent_len,
			wanted_send_len - sent_len, MSG_DONTWAIT);
		//S5LOG_DEBUG("send %d bytes", rc);
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
				S5LOG_FATAL("socket send return EFAULT, buffer addr:%p, len:%d", (char*)send_buf + sent_len, wanted_send_len - sent_len);
			}
			else
			{
				S5LOG_ERROR("send return rc:%d, %s need reconnect.", -errno, connection_info.c_str());
				writeable = FALSE;
				need_reconnect = TRUE;
				return -errno;
			}
		}
	}
	return 0;
}

int PfTcpConnection::do_send()
{
	int rc;
	do {
		if (send_bd == NULL) {
			if(send_q.is_empty()) {
				return 0;
			}
			S5Event evt;
			rc = send_q.get_event(&evt);
			if (unlikely(rc)) {
				S5LOG_ERROR("Get event from send_q failed");
			} else {
				rc = start_send((BufferDescriptor*) evt.arg_p);
				if (unlikely(rc)) {
					S5LOG_ERROR("start send failed, rc=%d",rc);
					return rc;
				}
				continue;
			}

		}
		rc = send_with_error_handle();
		if (unlikely(rc != 0 && rc != -EAGAIN))
		{
			S5LOG_ERROR("TCP send failed, rc:%d conn:%p (%s)", rc, this, connection_info.c_str());
			close();
			return -ECONNABORTED;
		}

		if (wanted_send_len == sent_len)
		{
			BufferDescriptor* temp_bd = send_bd;
			send_bd = NULL;

			rc = on_work_complete(temp_bd, WC_SUCCESS, this, temp_bd->cbk_data);
			dec_ref();
			if (unlikely(rc < 0))
			{
				S5LOG_ERROR("Failed on_work_complete rc:%d", rc);
				close();
				return rc;
			}
		}
	} while (writeable);

	return 0;

}

int PfTcpConnection::post_recv(BufferDescriptor *bd)
{
	bd->wr_op = WrOpcode::TCP_WR_RECV;
	bd->conn = this;
	add_ref();
	int rc = recv_q.post_event(EVT_RECV_REQ, 0, bd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed post_recv in connection:%s", connection_info.c_str());
		dec_ref();
	}
	return rc;
}

int PfTcpConnection::post_send(BufferDescriptor *bd)
{
	bd->wr_op = WrOpcode::TCP_WR_SEND;
	bd->conn = this;
	add_ref();
	int rc = send_q.post_event(EVT_SEND_REQ, 0, bd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed post_send in connection:%s", connection_info.c_str());
		dec_ref();
	}
	return rc;

}

int PfTcpConnection::post_read(BufferDescriptor *bd)
{
	S5LOG_FATAL("post_read should not used");
	return 0;
}

int PfTcpConnection::post_write(BufferDescriptor *buf)
{
	S5LOG_FATAL("post_write should not used");
	return 0;
}

static int pf_tcp_send_all(int fd, void* buf, int len, int flag)
{
	int off = 0;
	int rc = 0;
	while (off < len)
	{
		rc = (int)send(fd, (char*)buf + off, ssize_t(len - off), flag);
		if (rc == -1)
			return -errno;
		else if (rc == 0)
			return -ECONNABORTED;
		off += rc;
	}
	return len;
}

static int pf_tcp_recv_all(int fd, void* buf, int len, int flag)
{
	int off = 0;
	int rc = 0;
	while (off < len)
	{
		rc = (int)recv(fd, (char*)buf + off, ssize_t(len - off), flag);
		if (rc == -1)
			return -errno;
		else if (rc == 0)
			return -ECONNABORTED;
		off += rc;
	}
	return len;
}

PfTcpConnection* PfTcpConnection::connect_to_server(const std::string& ip, int port, PfPoller *poller, uint64_t vol_id, int& io_depth, int timeout_sec)
{
	Cleaner clean;
	int rc = 0;
	if(io_depth > PF_MAX_IO_DEPTH) {
		S5LOG_ERROR("io_depth:%d exceed max allowed:%d", io_depth, PF_MAX_IO_DEPTH);
		return NULL;
	}
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1)
	{
		rc = -errno;
		throw runtime_error(format_string("Failed to create socket, rc:%d", rc));
	}
	clean.push_back([socket_fd]() {::close(socket_fd); });
	int const1 = 1;
	rc = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&const1, sizeof(int));
	if (rc)
	{
		throw runtime_error("set TCP_NODELAY failed!");
	}

	rc = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &const1, sizeof(int));
	if (rc)
	{
		throw runtime_error("set SO_REUSEPORT failed!");
	}
	int sz = 1 << 20;
	rc = setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
	if (rc) {
		S5LOG_ERROR("Failed to set RCVBUF, rc:%d", errno);
	}

	rc = setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	if (rc) {
		S5LOG_ERROR("Failed to set SNDBUF to size:%d, rc:%d", sz, errno);
	}

	struct sockaddr_in addr;
	rc = parse_net_address(ip.c_str(), (uint16_t)port, &addr);
	if (rc)
	{
		throw runtime_error(format_string("parse_net_address failed on:%s rc:%d", ip.c_str(), rc));
	}
	//set the socket in non-blocking
	int fdopt = fcntl(socket_fd, F_GETFL);
	int new_option = fdopt | O_NONBLOCK;
	fcntl(socket_fd, F_SETFL, new_option);

	S5LOG_INFO("Connecting to %s:%d", ip.c_str(), port);
	rc = connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rc == 0)
	{
		S5LOG_INFO("connect to %s:%d OK", ip.c_str(), port);
	}
	else if (errno != EINPROGRESS)
	{
		throw runtime_error(format_string("Failed connect to %s:%d, rc:%d", ip.c_str(), port, errno));
	}
	else if (errno == EINPROGRESS)
	{
		S5LOG_INFO("waiting connect to %s:%d", ip.c_str(), port);
		struct pollfd poll_fd;
		memset(&poll_fd, 0, sizeof(poll_fd));
		poll_fd.fd = socket_fd;
		poll_fd.events = POLLOUT;
		int res = poll(&poll_fd, 1, timeout_sec * 1000);
		if (res > 0)
		{
			if (poll_fd.revents & POLLOUT)
			{
				//S5LOG_INFO("TCP connect success:%s", ip.c_str()); //may still fail on later state check
			}
			else
			{
				throw runtime_error(format_string("unexpected events:%d", poll_fd.revents));
			}
		}
		else if (res == 0)
		{
			throw runtime_error(format_string("TCP connect timeout and no events, peer:%s", ip.c_str()));
		}
		else if (res == -1)
		{
			throw runtime_error(format_string("TCP connect error, errno:%d", errno));
		}
	}
	int error = 0;
	socklen_t length = sizeof(error);
	if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
		throw runtime_error(format_string("getsockopt fail. errno:%d", errno));
	}
	if (error != 0)	{
		throw runtime_error(format_string("socket in error state:%d", error));
	}
	S5LOG_INFO("TCP connect success:%s, begin shake hands...", ip.c_str());
	fcntl(socket_fd, F_SETFL, fdopt);



	PfHandshakeMessage* hmsg = new PfHandshakeMessage;
	DeferCall _d([hmsg]() {delete hmsg; });
	
	memset(hmsg, 0, sizeof(PfHandshakeMessage));
	hmsg->hsqsize = (int16_t)io_depth;
	hmsg->vol_id = vol_id;
	hmsg->protocol_ver = PROTOCOL_VER;

	rc = pf_tcp_send_all(socket_fd, hmsg, sizeof(*hmsg), 0);
	if (rc == -1)
	{
		rc = -errno;
		throw runtime_error(format_string("Failed to send handshake data, rc:%d", rc));
	}
	rc = pf_tcp_recv_all(socket_fd, hmsg, sizeof(*hmsg), 0);
	if (rc == -1) {
		rc = -errno;
		throw runtime_error(format_string("Failed to receive handshake data, rc:%d", rc));
	}
	if (hmsg->hs_result != 0) {
		if (hmsg->hs_result == MSG_STATUS_INVALID_IO_TIMEOUT) {
			throw runtime_error(format_string("client's io_timeout setting is little than store's %d", hmsg->io_timeout));
		}
		S5LOG_ERROR("Connection rejected by server with result: %d", hmsg->hs_result);
		throw runtime_error(format_string("Connection rejected by server with result: %d", hmsg->hs_result));
	}
	S5LOG_DEBUG("Handshake complete, send iodepth:%d, receive iodepth:%d", io_depth, hmsg->crqsize);
	io_depth = hmsg->hsqsize;
	PfTcpConnection* conn = new PfTcpConnection(true);
	clean.push_back([conn]() {delete conn; });
	rc = conn->init(socket_fd, poller, io_depth*2, io_depth*2);
	if (rc != 0)
		throw runtime_error(format_string("Failed call connection init, rc:%d", rc));
	conn->state = CONN_OK;
	clean.cancel_all();
	return conn;
}

