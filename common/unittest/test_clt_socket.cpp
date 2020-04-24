

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

int recv_msg_read_reply(void* sockParam, s5_message_t* msg, void* param){
	int rc = 0;
	//S5TCPSocket* socket = (S5TCPSocket*)sockParam;
	if(msg && msg->head){
		S5LOG_INFO("CLT::READ_REPLY recv:msg_type:%d data_len:%d\n", msg->head->msg_type, msg->head->data_len);

		//handle stastic-info
		S5LOG_INFO("CLT::READ_REPLY data[0]:0x%x, data[%d]:0x%x\n", *((int*)msg->data), (msg->head->data_len - 4), *((int*)((char*)msg->data + msg->head->data_len - 4)));
	}
	else{
		S5LOG_INFO("CLT:: recv_msg_comm_reply  msg is NULL return.\n");
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




////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
	int rc = 0;
	if ((argc < 3) || (argc > 4)) { 	// Test for correct number of arguments
	  cerr << "Usage: " << argv[0] 
		   << " <Server>  [<Server Port>] <msg_type>" << endl;
	  rc = -1;
	  return rc;
	}
	char* servAddress = argv[1];
	unsigned short servPort = atoi(argv[2]);
	int msgType = (argc == 4) ? atoi(argv[3]) : MSG_TYPE_WRITE;
	
	S5LOG_INFO("Clt::begin... servAddress:%s srvPort:%d msgType:%d\n" ,argv[1], servPort, msgType);
	
	S5TCPSocket  clt;
	int autorcv = 1;
	clt.register_recv_handle(MSG_TYPE_READ_REPLY, recv_msg_read_reply, NULL);
	clt.register_recv_handle(MSG_TYPE_OPENIMAGE_REPLY, recv_msg_comm_reply, NULL);
	clt.register_recv_handle(MSG_TYPE_WRITE_REPLY, recv_msg_comm_reply, NULL);
	rc = clt.connect(servAddress, servPort, autorcv, CONNECT_TYPE_TEMPORARY);
	if(rc)
		S5LOG_INFO("connect:%d\n", rc);

	s5_message_t *msg_reply = NULL ;
	s5_message_t *msg= NULL;
	msg = (s5_message_t *)malloc(sizeof(s5_message_t));
	msg->head = (s5_message_head_t *)malloc(sizeof(s5_message_head_t));
	msg->head->magic_num = S5MESSAGE_MAGIC;
	msg->head->msg_type = MSG_TYPE_WRITE;
	msg->head->nlba = 0;
	msg->head->data_len = LBA_LENGTH;
	msg->data = malloc(msg->head->data_len);
	memset(msg->data, 0xED, msg->head->data_len);
	msg->tail = (s5_message_tail_t*)malloc(sizeof(s5_message_tail_t));

	char ch[8];
	int i = 0;
	int j = 0;
	do{
		msg->head->msg_type = (MSG_TYPE)msgType;
		if(autorcv || (msgType == MSG_TYPE_SNAP_CHANGED
			|| msgType == MSG_TYPE_OPENIMAGE_REPLY
			|| msgType == MSG_TYPE_READ_REPLY
			|| msgType == MSG_TYPE_WRITE_REPLY
			|| msgType == MSG_TYPE_TRIM_REPLY
			|| msgType == MSG_TYPE_LOAD_REPLY
			|| msgType == MSG_TYPE_FLUSHCOMPLETE
			|| msgType == MSG_TYPE_KEEPALIVE_REPLY)
		){
			S5LOG_INFO("===clt:: send_msg(%d) before...\n", msgType);
			if(msgType == MSG_TYPE_WRITE){
					static char val = 0;
					msg->head->msg_type = MSG_TYPE_WRITE;
					msg->head->slba = i;
					msg->head->nlba = 1;
					msg->head->status = MSG_STATUS_OK;
					msg->head->data_len = LBA_LENGTH;
					val++;
					i++;
					memset(msg->data, val, msg->head->data_len);
					S5LOG_INFO("CLT::WRITE    data:0x%x data_len:%d slba:%d\n", *((int*)msg->data), msg->head->data_len, msg->head->slba);
			}
			else if(msgType == MSG_TYPE_READ){
					msg->head->slba = j;
					msg->head->nlba = 1;
					msg->head->data_len = 0;
					j++;
					S5LOG_INFO("CLT::READ     data_len:%d slba:%d\n", msg->head->data_len, msg->head->slba);
			}
				
			rc = clt.send_msg(msg);
			S5LOG_INFO("===clt:: send_msg after rc:%d ...\n", rc);
		}
		else{
			S5LOG_INFO("clt:: send_msg_wait_reply(%d) before...\n", msgType);
			msg_reply = clt.send_msg_wait_reply(msg);
			S5LOG_INFO("clt:: send_msg_wait_reply after reply:%p...\n", msg_reply);
			if(msg_reply&&msg_reply->head){
				S5LOG_INFO("reply-msg type:%d nlba:%d status:%d\n", 
					msg_reply->head->msg_type, msg_reply->head->nlba, msg_reply->head->status);
			}
			else
				S5LOG_INFO("reply-msg err msg_reply:%p\n", msg_reply);

			printf("=====before release msg_reply\n");
			sleep(10);
			clt.release_reply_msg(msg_reply);
		}
		msgType = MSG_TYPE_WRITE;

		
		cin>>ch;
		if(ch[0] == 'x' || ch[0] == 'X')
			break;
CHECK:
		S5LOG_INFO("input msg-type:%d\n", atoi(ch));
		if(atoi(ch) < 0 || atoi(ch) > (MSG_TYPE_MAX - 1)){
			cout<<"Too bigger or smaller, invalid-type:" << ch<<endl;
			cin>>ch;
			goto CHECK;
		}
		else
			msgType = atoi(ch);
		#if 0
		#endif
	}while(ch!="x");

	if(msg){
		if(msg->tail)
			free(msg->tail);
		if(msg->data)
			free(msg->data);
		if(msg->head)
			free(msg->head);
		if(msg)
			free(msg);
		msg = NULL;
	}
	
	
	return 0;
}



