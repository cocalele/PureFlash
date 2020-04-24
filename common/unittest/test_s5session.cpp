#include <gtest/gtest.h>
#include "s5log.h"
#include "cmdopt.h"
#include "s5session.h"
#include "s5message.h"

pthread_mutex_t mutex;
pthread_cond_t cond;

void io_callback(void* arg)
{
	pthread_mutex_lock(&mutex);
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

TEST(S5Session, Read4k)
{
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
	s5_message_t* msg = s5msg_create(4096);
	msg->head.nlba=1;
	msg->head.msg_type=MSG_TYPE_FLUSH_READ;
	s5_session_t s5session;
	s5session_conf_t conf;
	conf.retry_delay_ms = 50;
	conf.rge_io_depth = 256;
	conf.s5_io_depth = 512;
	conf.rge_io_max_lbas = 256;
	
	ASSERT_EQ(0, s5session_init(&s5session, "127.0.0.1", 10000, CONNECT_TYPE_STABLE, &conf));
	ASSERT_EQ(0, s5session_aio_read(&s5session, msg, io_callback, msg));
	pthread_mutex_lock(&mutex);
	pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);
}
