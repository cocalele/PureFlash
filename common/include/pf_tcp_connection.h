#ifndef pf_tcp_connection_h__
#define pf_tcp_connection_h__
#include "pf_message.h"
#include "pf_connection.h"
#include "pf_event_queue.h"
#include "pf_buffer.h"
#include "pf_poller.h"
#include "pf_utils.h"

class PfPoller;
class PfClientVolume;
class BufferDescriptor;

class PfTcpConnection : public PfConnection
{
public:
	PfTcpConnection(bool is_client);
	virtual ~PfTcpConnection();
	virtual int post_recv(BufferDescriptor* buf);
	virtual int post_send(BufferDescriptor* buf);
	virtual int post_read(BufferDescriptor* buf);
	virtual int post_write(BufferDescriptor* buf);
	virtual int do_close();

	int start_send(BufferDescriptor* bd);
	int start_send(BufferDescriptor* bd, void* buf);
	int start_recv(BufferDescriptor* bd);
	int start_recv(BufferDescriptor* bd, void* buf);

	static void on_send_q_event(int fd, uint32_t event, void* c);
	static void on_recv_q_event(int fd, uint32_t event, void* c);
	static void on_socket_event(int fd, uint32_t event, void* c);
	static PfTcpConnection* connect_to_server(const std::string& ip, int port, PfPoller *poller,
											  uint64_t vol_id, int& io_depth, int timeout_sec);

	int init(int sock_fd, PfPoller *poller, int send_q_depth, int recv_q_depth);

	int socket_fd;
	PfPoller *poller;

	void* recv_buf;
	int recved_len;              ///< how many has received.
	int wanted_recv_len;		///< want received length.
	BufferDescriptor* recv_bd;

	void* send_buf;
	int sent_len;				///< how many has sent
	int wanted_send_len;		///< want to send length of data.
	BufferDescriptor*  send_bd;

	BOOL readable;              ///< socket buffer have data to read yes or no.
	BOOL writeable;             ///< socket buffer can send data yes or no.

	BOOL need_reconnect;

	PfEventQueue recv_q;
	PfEventQueue send_q;

	bool is_client; //is this a client side connection

private:
	int do_receive();
	int do_send();
	int rcv_with_error_handle();
	int send_with_error_handle();
	void flush_wr();
};

#endif // pf_tcp_connection_h__
