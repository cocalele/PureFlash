#include "s5socket.h"  		  // For Socket, ServerSocket, and SocketException
#include "s5log.h"
#include "s5session.h"
#include "s5message.h"
#include "spy.h"


#include <sys/types.h>		 // For data types
#include <sys/socket.h> 	 // For socket(), connect(), send(), and recv()
#include <netdb.h>			 // For gethostbyname()
#include <arpa/inet.h>		 // For inet_addr()
#include <unistd.h> 		 // For close()
#include <netinet/in.h> 	 // For sockaddr_in

#include <errno.h>             // For errno
#include <stdlib.h>
#include <stdio.h>

#define SEND_IO_CNT		600*1000

long g_io_count = 0;
pthread_mutex_t g_io_lock;


void io_callback(void* arg)
{
	s5_message_t *msg= (s5_message_t *)arg;
	if(!msg)
	{
		S5LOG_ERROR("io_callback param is invalid.");
		return;
	}
	/*
	LOG_INFO("------------CMD[%d] tid[%d] slba[%lld] data_len[%d]-----------"
		, msg->head->msg_type
		, msg->head->transaction_id
		, msg->head->slba
		, msg->head->data_len);
	*/
	s5msg_release_all(&msg);
	pthread_mutex_lock(&g_io_lock);
	g_io_count--;
	pthread_mutex_unlock(&g_io_lock);	
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
	long i = 0;
	int j = 0;
	int k;
	s5_session_t s5session;
	
	if ((argc < 3) || (argc > 4)) { 	// Test for correct number of arguments
	  printf("Usage: %s", argv[0]);
	  printf("\t <Server IP>  <Server Port> <block_size>\n");
	  rc = -1;
	  return rc;
	}
	pthread_mutex_init(&g_io_lock, NULL);
	
	servAddress = argv[1];
	servPort = atoi(argv[2]);
	int	block_size = (argc == 4) ? atoi(argv[3]) : LBA_LENGTH; 
	if(block_size%LBA_LENGTH != 0)
	{
		S5LOG_ERROR("block size must be aligned by 4096.");
		return -1;
	}
	spy_start(30000);
	s5session_conf_t conf;
	conf.retry_delay_ms = 50;
	conf.rge_io_depth = 256;
	conf.s5_io_depth = 512;
	conf.rge_io_max_lbas = 256;
	conf.use_vdriver = 1;
	S5LOG_INFO("Clt::begin... servAddress:%s srvPort:%d msgType:%d\n" ,argv[1], servPort, msgType);
	rc = s5session_init(&s5session, servAddress, servPort, CONNECT_TYPE_STABLE, &conf);
	if(rc){
		S5LOG_ERROR("s5session_init err %d.", rc);
		return rc;
	}

	S5LOG_INFO("start send msg bs:%d.", block_size);
	//msg = s5msg_create(block_size);
    for(i = 0; i < SEND_IO_CNT; i++)
	{
		msg = s5msg_create(block_size);
		msg->head->slba = i;
		msg->head->nlba=1;
		msg->head->transaction_id=i;
		msg->head->msg_type=MSG_TYPE_WRITE;
RETRY:
		rc = s5session_aio_read(&s5session, msg, io_callback, msg);
		if(rc){
			if(rc == -EAGAIN){
				//sleep(1);
				//LOG_INFO("io_id[%d] need to retry.", i);
				goto RETRY;
			}
			S5LOG_ERROR("s5session_aio_read err %d.", rc);
			return rc;
		}
		pthread_mutex_lock(&g_io_lock);
		g_io_count++;
		pthread_mutex_unlock(&g_io_lock);
    }
	S5LOG_INFO("send msg finished 0  io[%d].", SEND_IO_CNT);
	pthread_mutex_lock(&g_io_lock);
	while(g_io_count != 0)
	{
		//LOG_INFO("io count[%d] need to finish.", g_io_count);
		pthread_mutex_unlock(&g_io_lock);
		usleep(200*1000);	
		//LOG_INFO("io count[%d] need to finish after.", g_io_count);
	}
	pthread_mutex_unlock(&g_io_lock);
	//s5msg_release_all(&msg);
	S5LOG_INFO("send msg finished 1.");
	return 0;
}



