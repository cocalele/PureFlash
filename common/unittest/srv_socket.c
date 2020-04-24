#include "s5socket.h"  		  // For Socket, ServerSocket, and SocketException
#include "s5log.h"

#include <sys/types.h>		 // For data types
#include <sys/socket.h> 	 // For socket(), connect(), send(), and recv()
#include <netdb.h>			 // For gethostbyname()
#include <arpa/inet.h>		 // For inet_addr()
#include <unistd.h> 		 // For close()
#include <netinet/in.h> 	 // For sockaddr_in

#include <errno.h>             // For errno

const int g_port = 20000;
const char* g_ip = "127.0.0.1";
PS5SRVSOCKET g_sock = NULL;
int g_flag = 0;


int recv_msg_login(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_read(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_write(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_trim(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_load(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_flush(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_keepalive(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_snapchange(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_getsysinfo(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_getstasticinfo(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_comm_reply(void* sockParam, s5_message_t* msg, void* param);
int recv_msg_close(void* sockParam, s5_message_t* msg, void* param);


void register_handle(PS5SRVSOCKET	sock);
void unregister_handle(PS5SRVSOCKET	sock);

////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
	int rc = 0;
	unsigned short servPort;
	PS5CLTSOCKET cltSock = NULL;

	char ch;
	char*	ipaddr;
	#if 0
	if ((argc < 3)) { 	// Test for correct number of arguments
      printf("Usage: %s", argv[0]);
	  printf("\t <Server IP> <Server Port>\n");
	  
	  rc = -1;
	  return rc;
	}
	#endif
	servPort = g_port;
	ipaddr = (char*)g_ip;
	
	g_sock = (PS5SRVSOCKET)s5socket_create(SOCKET_TYPE_SRV, servPort, ipaddr);
	if(!g_sock)
		S5LOG_INFO("socket_create err:%d\n", rc);
	S5LOG_INFO("Srv:: listening port:%d ...\n", servPort);

	//reigster.
	register_handle(g_sock);
	while(g_flag == 0){
		cltSock = s5socket_accept(g_sock, 1); 
		if(!cltSock)
			S5LOG_INFO("Accept err!\n");
		
		S5LOG_ERROR("Accept daemon<ip:%s port:%d>.", s5socket_get_foreign_ip(cltSock), s5socket_get_foreign_port(cltSock));
	}
	return rc;
}



//////////////////////////////////////////////////////////////
void register_handle(PS5SRVSOCKET	sock)
{
	if(!sock)
		return;
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_OPENIMAGE, recv_msg_login, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_CLOSEIMAGE, recv_msg_close, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_READ, recv_msg_read, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_WRITE, recv_msg_write, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_TRIM, recv_msg_trim, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_LOAD_REQUEST, recv_msg_load, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_FLUSH_REQUEST, recv_msg_flush, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_KEEPALIVE, recv_msg_keepalive, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_SNAP_CHANGED, recv_msg_snapchange, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_OPENIMAGE_REPLY, recv_msg_comm_reply, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_READ_REPLY, recv_msg_comm_reply, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_WRITE_REPLY, recv_msg_comm_reply, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_TRIM_REPLY, recv_msg_comm_reply, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_LOAD_REPLY, recv_msg_comm_reply, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_FLUSHCOMPLETE, recv_msg_comm_reply, NULL);
	s5socket_register_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_KEEPALIVE_REPLY, recv_msg_comm_reply, NULL);
}

void unregister_handle(PS5SRVSOCKET	sock)
{
	if(!sock)
		return;
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_OPENIMAGE);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_CLOSEIMAGE);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_READ);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_WRITE);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_TRIM);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_LOAD_REQUEST);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_FLUSH_REQUEST);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_KEEPALIVE);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_SNAP_CHANGED);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_OPENIMAGE_REPLY);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_READ_REPLY);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_WRITE_REPLY);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_TRIM_REPLY);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_LOAD_REPLY);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_FLUSHCOMPLETE);
	s5socket_unreigster_handle(sock, SOCKET_TYPE_SRV, MSG_TYPE_KEEPALIVE_REPLY);
}

int recv_msg_close(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		
		if(msg->head.msg_type == MSG_TYPE_CLOSEIMAGE){
			g_flag = 1;
			unregister_handle(g_sock);
			
			s5socket_release(&g_sock, SOCKET_TYPE_SRV);
			S5LOG_INFO("recv_msg_close...");
		}
	}
	else{
		S5LOG_INFO("recv_msg_login  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}



int recv_msg_login(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		S5LOG_INFO("recv:data[0]:0x%x, data[%d]:0x%x\n", *((int*)msg->data), (msg->head.nlba * LBA_LENGTH - 4), *((int*)((char*)msg->data + msg->head.nlba*LBA_LENGTH - 4)));
		S5LOG_INFO("recv:tail->flag:%d tail->crc:%d\n", msg->tail.flag, msg->tail.crc);
		msg->head.msg_type = MSG_TYPE_OPENIMAGE_REPLY;
		msg->head.nlba = 0;
		msg->head.status = MSG_STATUS_OK;
		
		rc = s5socket_send_msg(socket, msg);
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
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;

	if(msg) {
		S5LOG_INFO("SRV::READ_REQ  :msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.data_len);
		msg->head.msg_type = MSG_TYPE_READ_REPLY;
		msg->head.nlba = 1;
		if(msg->head.data_len == 0)
		{
			msg->data = malloc(LBA_LENGTH);
		}
		msg->head.status = MSG_STATUS_OK;
		msg->head.data_len = LBA_LENGTH;
		val++;
		memset(msg->data, val, msg->head.data_len);
		rc = s5socket_send_msg(socket, msg);
		S5LOG_INFO("recv-send-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_read  msg is NULL return.\n");
		rc = -1;
	}

	s5msg_release_all(&msg);
	return rc;
}

int recv_msg_write(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;

	if(msg){
		S5LOG_INFO("RCV: write slba:%d\n", msg->head.slba);
		msg->head.msg_type = MSG_TYPE_WRITE_REPLY;
		msg->head.data_len = 0;
		msg->head.status = MSG_STATUS_OK;
		rc = s5socket_send_msg(socket, msg);
		S5LOG_INFO("RCV: write-reply:rc:%d\n", rc);
	}
	else{
		S5LOG_INFO("recv_msg_write  msg is NULL return.\n");
		rc = -1;
	}
	s5msg_release_all(&msg);
	return rc;
}

int recv_msg_trim(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;

	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		msg->head.msg_type = MSG_TYPE_TRIM_REPLY;
		msg->head.nlba = 0;
		msg->head.status = MSG_STATUS_OK;
		rc = s5socket_send_msg(socket, msg);
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
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;

	if(msg){
		//LOG_ERROR("recv:load_request slba:%d.", msg->head->slba);
	}
	else{
		S5LOG_INFO("recv_msg_load  msg is NULL return.\n");
		rc = -1;
	}
	s5msg_release_all(&msg);
	return rc;
}

int recv_msg_flush(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		msg->head.msg_type = MSG_TYPE_FLUSHCOMPLETE_REPLY;
		msg->head.nlba = 0;
		msg->head.status = MSG_STATUS_OK;
		rc = s5socket_send_msg(socket, msg);
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
	PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		msg->head.msg_type = MSG_TYPE_KEEPALIVE_REPLY;
		msg->head.nlba = 0;
		msg->head.status = MSG_STATUS_OK;
		rc = s5socket_send_msg(socket, msg);
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
	//PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg)
	{
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		//handle snap-changed
	}
	else{
		S5LOG_INFO("recv_msg_snapchange  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_getsysinfo(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		//handle sys-info
	}
	else{
		S5LOG_INFO("recv_msg_getsysinfo  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_getstasticinfo(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		//handle stastic-info
	}
	else{
		S5LOG_INFO("recv_msg_getstasticinfo  msg is NULL return.\n");
		rc = -1;
	}

	return rc;
}

int recv_msg_comm_reply(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//PS5CLTSOCKET socket = (PS5CLTSOCKET)sockParam;
	if(msg){
		S5LOG_INFO("recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.nlba);
		//handle stastic-info
	}
	else{
		S5LOG_INFO("recv_msg_comm_reply  msg is NULL return.\n");
		rc = -1;
	}
	return rc;
}






