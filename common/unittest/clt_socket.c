#include "s5socket.h"  		  // For Socket, ServerSocket, and SocketException
#include "s5log.h"
#include "s5errno.h"

#include <sys/types.h>		 // For data types
#include <sys/socket.h> 	 // For socket(), connect(), send(), and recv()
#include <netdb.h>			 // For gethostbyname()
#include <arpa/inet.h>		 // For inet_addr()
#include <unistd.h> 		 // For close()
#include <netinet/in.h> 	 // For sockaddr_in

#include <errno.h>             // For errno
#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>

#define SEND_IO_CNT		10

const int g_port = 20000;
const char* g_ip = "127.0.0.1";
const int g_bs = 4096;
sem_t	g_sem;



int s5d_send_msg(PS5CLTSOCKET *socket, s5_message_t* msg, char* ip_addr, unsigned short port, int waitreply)
{
	int rc = -1;

	//create socket.
	if(!*socket)
		*socket = s5socket_create(SOCKET_TYPE_CLT, 0, NULL);
	if(waitreply)
	{
		s5_message_t*	reply = NULL;
		reply = s5socket_send_msg_wait_reply(*socket, msg);
		if(!reply)
		{
			rc = -S5_ENETWORK_EXCEPTION;
			S5LOG_ERROR("s5d_send_msg: send_wait_reply err r_ip(%s) r_port(%d).", ip_addr, port);
		}
	}
	else
	{
		rc = s5socket_send_msg(*socket, msg);
		if(rc)
		{
			S5LOG_ERROR("s5d_send_msg: send err(%d) r_ip(%s) r_port(%d).", rc, ip_addr, port);
		}
	}

	return rc;
}


int recv_msg_read_reply(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg){
		S5LOG_INFO("CLT::READ_REPLY recv:msg_type:%d data_len:%d\n", msg->head.msg_type, msg->head.data_len);
		if(msg->head.slba == (SEND_IO_CNT-1))
			sem_post(&g_sem);
		//handle stastic-info
		S5LOG_INFO("CLT::READ_REPLY data[0]:0x%x, data[%d]:0x%x\n", *((int*)msg->data), (msg->head.data_len - 4), *((int*)((char*)msg->data + msg->head.data_len - 4)));
	}
	else{
		S5LOG_INFO("CLT:: recv_msg_comm_reply  msg is NULL return.\n");
		rc = -1;
	}
	return rc;
}

int recv_msg_write_reply(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	if(msg){
		S5LOG_INFO("CLT:: recv write reply slba:%d", msg->head.slba);
		if(msg->head.slba == (SEND_IO_CNT-1)){
			S5LOG_INFO("CLT:: signal main loop exit.");
			sem_post(&g_sem);
		}
	}
	else{
		S5LOG_INFO("CLT:: recv_msg_comm_reply  msg is NULL return.\n");
		rc = -1;
	}
	return rc;
}


int recv_msg_comm_reply(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	if(msg)
	{
	}
	else{
		S5LOG_INFO("recv_msg_comm_reply  msg is NULL return.\n");
		rc = -1;
	}
	return rc;
}


////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
	int rc = 0;
	char* servAddress = NULL;
	unsigned short servPort;
	int msgType;
	PS5CLTSOCKET clt;
	int autorcv = 1;
	s5_message_t *msg_reply = NULL ;
	s5_message_t *msg= NULL;
	int i = 0;
	int j = 0;
	int k;
	#if 0
	if ((argc < 3) || (argc > 4)) { 	// Test for correct number of arguments
	  printf("Usage: %s", argv[0]);
	  printf("\t <Server IP>  <Server Port> <msg_type>\n");
	  rc = -1;
	  return rc;
	}
	#endif
	sem_init(&g_sem, 0, 0);
	servAddress = (char*)g_ip;
	servPort = g_port;
	int	block_size = g_bs; 
	msgType = MSG_TYPE_WRITE;
	if(block_size%LBA_LENGTH != 0)
	{
		S5LOG_ERROR("block size must be aligned by 4096.");
		return -1;
	}
	
	S5LOG_INFO("Clt::begin... servAddress:%s srvPort:%d msgType:%d\n" ,servAddress, servPort, msgType);
	clt = (PS5CLTSOCKET)s5socket_create(SOCKET_TYPE_CLT, 0, NULL);
	s5socket_register_handle(clt, SOCKET_TYPE_CLT, MSG_TYPE_READ_REPLY, recv_msg_read_reply, NULL);
	s5socket_register_handle(clt, SOCKET_TYPE_CLT, MSG_TYPE_OPENIMAGE_REPLY, recv_msg_comm_reply, NULL);
	s5socket_register_handle(clt, SOCKET_TYPE_CLT, MSG_TYPE_WRITE_REPLY, recv_msg_write_reply, NULL);
	rc = s5socket_connect(clt, servAddress, NULL,
						  servPort, 0,
						  (s5_rcv_type_t)autorcv, CONNECT_TYPE_TEMPORARY);
	if(rc)
	{
		S5LOG_ERROR("connect:%d (%s:%d)\n", rc, servAddress, servPort);
		return rc;
	}
	msg = s5msg_create(block_size);
	S5LOG_INFO("STABLE:send msg start bs:%d.", block_size);
	msg->head.msg_type = msgType;
	for(k = 0; k < SEND_IO_CNT; k++)
	{
		msg->head.slba = k;
		rc = s5socket_send_msg(clt, msg);
		if(rc)
			break;
	}

	//notify server to exit.
	msg->head.msg_type = MSG_TYPE_CLOSEIMAGE;
	rc = s5socket_send_msg(clt, msg);
	
	S5LOG_INFO("STABLE:send msg(%d) finish OK.", k);
	sem_wait(&g_sem);
	s5msg_release_all(&msg);
	s5socket_unreigster_handle(clt, SOCKET_TYPE_CLT, MSG_TYPE_READ_REPLY);
	s5socket_unreigster_handle(clt, SOCKET_TYPE_CLT, MSG_TYPE_OPENIMAGE_REPLY);
	s5socket_unreigster_handle(clt, SOCKET_TYPE_CLT, MSG_TYPE_WRITE_REPLY);
	
	s5socket_release(&clt, SOCKET_TYPE_CLT);
	return 0;
}



