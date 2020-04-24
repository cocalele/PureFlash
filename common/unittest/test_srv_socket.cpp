

#include <iostream>           // For cerr and cout
#include <cstdlib>            // For atoi()

#include "s5socket.h"  		  // For Socket, ServerSocket, and SocketException

#include <sys/types.h>		 // For data types
#include <sys/socket.h> 	 // For socket(), connect(), send(), and recv()
#include <netdb.h>			 // For gethostbyname()
#include <arpa/inet.h>		 // For inet_addr()
#include <unistd.h> 		 // For close()
#include <netinet/in.h> 	 // For sockaddr_in

#include <errno.h>             // For errno


using namespace std;

int recv_msg_login(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_read(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_write(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_trim(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_load(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_flush(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_keepalive(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_snapchange(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_comm_reply(void* sockParam, s5_message_t* msg, void* param);

void register_handle(S5TCPServerSocket	*sock);



////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
	int rc = 0;
	if ((argc < 2)) { 	// Test for correct number of arguments
	  cerr << "Usage: " << argv[0] 
		   << " [<Server Port>]" << endl;
	  rc = -1;
	  return rc;
	}
	
	unsigned short servPort = atoi(argv[1]);
	S5TCPServerSocket	sock;
	S5TCPSocket*	cltSock;
	rc = sock.initServer(servPort);
	if(rc)
		S5LOG_INFO("iniServer err:%d\n", rc);
	S5LOG_INFO("Srv:: listening port:%d ...\n", servPort);

	//reigster.
	register_handle(&sock);
	
	while(1){
		cltSock = sock.accept(1);
		if(!cltSock)
			S5LOG_INFO("Accept err!\n");
	}
	char ch;
	cin>>ch;
	return rc;
}



//////////////////////////////////////////////////////////////
void register_handle(S5TCPServerSocket	*sock)
{
	if(!sock)
		return;
	sock->register_recv_handle(MSG_TYPE_OPENIMAGE, recv_msg_login, NULL);
	sock->register_recv_handle(MSG_TYPE_READ, recv_msg_read, NULL);
	sock->register_recv_handle(MSG_TYPE_WRITE, recv_msg_write, NULL);
	sock->register_recv_handle(MSG_TYPE_TRIM, recv_msg_trim, NULL);
	sock->register_recv_handle(MSG_TYPE_LOAD_REQUEST, recv_msg_load, NULL);
	sock->register_recv_handle(MSG_TYPE_FLUSH_REQUEST, recv_msg_flush, NULL);
	sock->register_recv_handle(MSG_TYPE_KEEPALIVE, recv_msg_keepalive, NULL);
	sock->register_recv_handle(MSG_TYPE_SNAP_CHANGED, recv_msg_snapchange, NULL);

	sock->register_recv_handle(MSG_TYPE_OPENIMAGE_REPLY, recv_msg_comm_reply, NULL);
	sock->register_recv_handle(MSG_TYPE_READ_REPLY, recv_msg_comm_reply, NULL);
	sock->register_recv_handle(MSG_TYPE_WRITE_REPLY, recv_msg_comm_reply, NULL);
	sock->register_recv_handle(MSG_TYPE_TRIM_REPLY, recv_msg_comm_reply, NULL);
	sock->register_recv_handle(MSG_TYPE_LOAD_REPLY, recv_msg_comm_reply, NULL);
	sock->register_recv_handle(MSG_TYPE_FLUSHCOMPLETE, recv_msg_comm_reply, NULL);
	sock->register_recv_handle(MSG_TYPE_KEEPALIVE_REPLY, recv_msg_comm_reply, NULL);
}



int recv_msg_login(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		S5LOG_INFO("recv:data[0]:0x%x, data[%d]:0x%x\n", *((int*)msg->data), (msg->head->nlba*LBA_LENGTH - 4), *((int*)((char*)msg->data + msg->head->nlba*LBA_LENGTH - 4)));
		S5LOG_INFO("recv:tail->flag:%d tail->crc:%d\n", msg->tail->flag, msg->tail->crc);
		msg->head->msg_type = MSG_TYPE_OPENIMAGE_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_login  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_read(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	static char val = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("SRV::READ_REQ  :msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		msg->head->msg_type = MSG_TYPE_READ_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		msg->head->data_len = LBA_LENGTH;
		val++;
		memset(msg->data, val, msg->head->data_len);
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_read  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_write(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		msg->head->msg_type = MSG_TYPE_WRITE_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_write  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_trim(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		msg->head->msg_type = MSG_TYPE_TRIM_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_trim  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_load(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		msg->head->msg_type = MSG_TYPE_LOAD_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_load  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_flush(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		msg->head->msg_type = MSG_TYPE_FLUSHCOMPLETE_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_flush  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_keepalive(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		msg->head->msg_type = MSG_TYPE_KEEPALIVE_REPLY;
		msg->head->nlba = 0;
		msg->head->status = MSG_STATUS_OK;
		rc = socket->send_msg(msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_keepalive  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_snapchange(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		//handle snap-changed
	}
	else{
		S5LOG_INFO("recv_msg_snapchange  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_comm_reply(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->nlba);
		//handle stastic-info
	}
	else{
		S5LOG_INFO("recv_msg_comm_reply  msg is NULL return.\n");
		rc = -1;
	}
	return rc;
}






