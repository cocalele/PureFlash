#include <gtest/gtest.h>
#include "s5log.h"
#include "cmdopt.h"
#include <pthread.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <sys/epoll.h>
#include <signal.h>
#include <semaphore.h>
#include <arpa/inet.h>		 // For inet_addr()
#include <unistd.h> 		 // For close()
#include <netinet/in.h> 	 // For sockaddr_in
#include <sys/eventfd.h>
#include <time.h>
#include <errno.h>

S5LOG_INIT("LogTest");

static void sig_usr(int signo)      /* argument is signal number */
{
	if (signo == SIGUSR1)
		printf("received SIGUSR1\n");
	else if (signo == SIGUSR2)
		printf("received SIGUSR2\n");
	else
		printf("received signal %d\n", signo);
}

int signal_handler_set(int sig, void (*sa_handler2)(int))
{
	struct sigaction sa={{0}};
	sa.sa_handler=SIG_IGN;//sa_handler2;
	return sigaction(sig, &sa, NULL);
}

TEST(LogTest, Basic) 
{
	S5LOG_DEBUG("Debug with no arg");
	S5LOG_DEBUG("Debug with 1 int arg=%d", 1);
	S5LOG_INFO("Info with 1 int arg=%d, str arg=%s", 1, "'Hello world'");
	S5LOG_WARN("Warn with no args");
	S5LOG_ERROR("Error with no args");
	S5LOG_TRACE("Trace with no args");
}

static void *log_deadlock_thread(void *param) 
{
	signal_handler_set(SIGUSR1, sig_usr);
	pthread_t threadID = pthread_self();
	// Guarantees that thread resources are deallocated upon return  
	pthread_detach(threadID); 
	time_t seconds = time(NULL);
	while(1)
	{
		S5LOG_DEBUG("Child id:%d - Debug with no arg", (int)threadID);
		S5LOG_DEBUG("Child id:%d - Debug with 1 int arg=%d", (int)threadID, 1);
		S5LOG_INFO("Child id:%d - Info with 1 int arg=%d, str arg=%s", (int)threadID, 1, "'Hello world'");
		S5LOG_WARN("Child id:%d - Warn with no args", (int)threadID);
		S5LOG_ERROR("Child id:%d - Error with no args", (int)threadID);
		usleep((long long)param * 3000);
		if(time(NULL) > seconds+10)
			break;
	}
	return NULL;
}
TEST(LogTest, DeadLock) 
{
	pthread_t threadID[10];
	for(unsigned i=0;i<sizeof(threadID)/sizeof(threadID[0]); i++)
	{
		ASSERT_EQ(0, pthread_create(&threadID[i], NULL, log_deadlock_thread, (void*)i));

	}

	time_t seconds = time(NULL);
	while(1)
	{
		S5LOG_DEBUG("===== Debug with no arg");
		S5LOG_DEBUG("=====- Debug with 1 int arg=%d", 1);
		S5LOG_INFO("=====- Info with 1 int arg=%d, str arg=%s", 1, "'Hello world'");
		S5LOG_WARN("=====- Warn with no args");
		S5LOG_ERROR("=====- Error with no args");
		usleep(5000);
		if(time(NULL) > seconds+3)
		{
			for(unsigned i=0;i<sizeof(threadID)/sizeof(threadID[0]); i++)
			{
				//pthread_cancel(threadID[i]); //this may cause log4c dead lock
				pthread_kill(threadID[i], SIGUSR1); //this will not
				pthread_join(threadID[i], NULL);

			}
		}
		if(time(NULL) > seconds+4)
		{
			return;
		}
	}
}


static const char* largv[]={"exe", "-a", "a v", "--i", "1", "--flag" , "noname", "--d" , " error double"};
TEST(OptTest, Basic)
{
	ASSERT_EQ(0, opt_initialize(sizeof(largv)/sizeof(largv[0]), largv));

	//get arguemt -a
	ASSERT_TRUE(opt_error_code()==0 && opt_has_next());
	ASSERT_STREQ("a",  opt_next());
	ASSERT_EQ(largv[2], opt_value());

	//get argument i
	ASSERT_TRUE(opt_error_code()==0 && opt_has_next());
	ASSERT_STREQ("i",  opt_next());
	ASSERT_EQ(1, opt_value_as_int());

	//get argument flag
	ASSERT_TRUE(opt_error_code()==0 && opt_has_next());
	ASSERT_STREQ("flag",  opt_next());
	//ASSERT_EQ(NULL, opt_value());

	ASSERT_TRUE(opt_error_code()==0 && opt_has_next());
	ASSERT_EQ(NULL,  opt_next());
	ASSERT_STREQ("noname", opt_value());

	ASSERT_TRUE(opt_error_code()==0 && opt_has_next());
	ASSERT_STREQ("d",  opt_next());
	opt_value_as_double();
	ASSERT_EQ(OPT_FAILCONVERT, opt_error_code());

	ASSERT_FALSE(opt_has_next());

	opt_uninitialize();

    
}

static sem_t sem;
static void *sem_producer(void *paramSock) 
{
	pthread_t threadID = pthread_self();
	// Guarantees that thread resources are deallocated upon return  
	pthread_detach(threadID); 
	int count=100010;

	for(int i=0;i<count;i++)
	{
		sem_post(&sem);
	}

	return NULL;
}
TEST(Semaphore, Performance)
{
	pthread_t wakeup_thread;
	int count=100000;
	ASSERT_EQ(0, sem_init(&sem, 0, 0));
	ASSERT_EQ(0, pthread_create(&wakeup_thread, NULL, sem_producer, NULL));
	struct timeval start;
	struct timeval end;
	float timeelapse = 0;
	gettimeofday(&start, NULL);
	for(int i=0;i<count;i++)
	{
		sem_wait(&sem);
	}
	gettimeofday(&end, NULL);
	timeelapse = (end.tv_sec - start.tv_sec) * 1000000
                  + (end.tv_usec - start.tv_usec);
	printf("Semaphore wait %d times, elapsed %f usec\n", count, timeelapse);
}


static void *sock_thread(void *paramSock) 
{
	pthread_t threadID = pthread_self();
	// Guarantees that thread resources are deallocated upon return  
	pthread_detach(threadID); 

	if(-1 == signal_handler_set(SIGUSR1, sig_usr))
	{
		printf("Set sig handler fial");
		return NULL;
	}

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in localAddr;
	int rc = 0;
	localAddr.sin_family = AF_INET;
	localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	localAddr.sin_port = htons(10010);
	bind(sfd, (sockaddr *) &localAddr, sizeof(sockaddr_in));
	listen(sfd, 5);
	int epfd = epoll_create(1);
	static struct epoll_event ev;
	ev.events=EPOLLIN|EPOLLPRI|EPOLLERR|EPOLLHUP;
	ev.data.fd=sfd;
	rc=epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev);
	epoll_event rev;

	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if( rc)
		printf("- pthread_sigmask failed\n");


	sigset_t sigset2;
	sigemptyset(&sigset2);

	while(1)
	{
		printf("- sleep(2)\n");
		sleep(2);
		printf("- epoll_pwait\n");
		int nfds = epoll_pwait(epfd, &rev, 1, -1, &sigset2);
		printf("- epoll_wait return :%d\n", nfds);
		/*if(nfds == -1)
		{
			if(EINTR == errno())
				printf("- 
		}*/
	}
	return NULL;
}
TEST(Thread_kill, Performance)
{
	pthread_t poll_thread;
	int count=100000;
	ASSERT_EQ(0, pthread_create(&poll_thread, NULL, sock_thread, NULL));
	sleep(1);
	struct timeval start;
	struct timeval end;
	float timeelapse = 0;
	gettimeofday(&start, NULL);
	for(int i=0;i<count;i++)
	{
		//printf("+ send SIGUSR1\n");
		pthread_kill(poll_thread, SIGUSR1);
	}
	gettimeofday(&end, NULL);
	timeelapse = (end.tv_sec - start.tv_sec) * 1000000
                  + (end.tv_usec - start.tv_usec);
	printf("+ send pthred_kill %d times, elapsed %f usec\n", count, timeelapse);
	fflush(stdout);
	sleep(5);
}

TEST(SpinLock, Performance)
{
	//pthread_t poll_thread;
	int count=1000000;
	pthread_spinlock_t spin;
	pthread_spin_init(&spin, 0);

	struct timeval start;
	struct timeval end;
	float timeelapse = 0;
	gettimeofday(&start, NULL);
	for(int i=0;i<count;i++)
	{
		pthread_spin_lock(&spin);
		pthread_spin_unlock(&spin);
	}
	gettimeofday(&end, NULL);
	timeelapse = (end.tv_sec - start.tv_sec) * 1000000
                  + (end.tv_usec - start.tv_usec);
	printf("+ call pthread_spin_lock %d times, elapsed %f usec\n", count, timeelapse);
	fflush(stdout);
}


TEST(EpollEdge, Function)
{
	if(-1 == signal_handler_set(SIGUSR1, sig_usr))
	{
		printf("Set sig handler fial");
		return ;
	}

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in localAddr;
	int rc = 0;
	localAddr.sin_family = AF_INET;
	localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	localAddr.sin_port = htons(10010);

	connect(sfd, (sockaddr *) &localAddr, sizeof(sockaddr_in));
	int epfd = epoll_create(1);
	static struct epoll_event ev;
	ev.events=EPOLLIN|EPOLLPRI|EPOLLERR|EPOLLHUP|EPOLLET|EPOLLRDHUP;
	ev.data.fd=sfd;
	rc=epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev);
	epoll_event rev;

	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if( rc)
		printf("- pthread_sigmask failed\n");


	sigset_t sigset2;
	sigemptyset(&sigset2);

	char buf[64];
	ssize_t count;
	while(( count = recv(sfd, buf, sizeof(buf)-1, MSG_DONTWAIT)) >= 0);
	if(count == -1)
	{
		printf("recv return: %d %s\n", errno, errno==EAGAIN ? "WOULDBLOCK" : strerror(errno));
	}

	int zero_count=0;
	while(1)
	{
		printf("Send data in nc, in 5 secs\n");
		sleep(5);
		printf("call epoll_pwait\n");
		int nfds = epoll_pwait(epfd, &rev, 1, -1, &sigset2);
		printf("- epoll_wait return :%d\n", nfds);
		if(nfds > 0)
		{
			if(rev.events & EPOLLIN)
			{
				printf("- epoll_wait get event EPOLLIN\n");

				while(( count = recv(sfd, buf, sizeof(buf), MSG_DONTWAIT)) > 0)
				{
					if(count == 0)
					{
						zero_count ++;
						if(zero_count > 20)
						{
							printf ("Too many zero call\n");
							return;
						}
					}
					buf[count] = 0;
					printf("%s\n", buf); 
				};
				if(count == -1)
				{
					printf("recv return: %d %s\n", errno, errno==EAGAIN ? "WOULDBLOCK" : strerror(errno));
				}
			}
			if(rev.events & EPOLLERR)
			{
				printf("- epoll_wait get event EPOLLERR\n");
			}
			if(rev.events & EPOLLHUP)
			{
				printf("- epoll_wait get event EPOLLHUP\n");
			}
			if(rev.events & EPOLLRDHUP)
			{
				printf("- epoll_wait get event EPOLLRDHUP\n");
			}
		}

	}
	
}

static int event_fd =0;
static void *envntfd_thread(void *paramSock) 
{
	pthread_t threadID = pthread_self();
	// Guarantees that thread resources are deallocated upon return  
	pthread_detach(threadID); 

	int epfd = epoll_create(1);
	static struct epoll_event ev;
	ev.events=EPOLLIN|EPOLLPRI|EPOLLERR|EPOLLHUP|EPOLLRDHUP;
	ev.data.fd=event_fd;
	int rc=epoll_ctl(epfd, EPOLL_CTL_ADD, event_fd, &ev);
	if(rc != 0)
	{
		printf("- epoll_ctl failed\n");
		return NULL;
	}
	epoll_event rev;

	while(1)
	{
		printf("- sleep(1)\n");
		sleep(1);
		printf("- epoll_pwait\n");
		int nfds = epoll_pwait(epfd, &rev, 1, -1, NULL);
		printf("- epoll_wait return :%d\n", nfds);
		if(nfds == -1)
		{
			if(EINTR == errno)
				return NULL;
			else
				perror("- epoll_wait error");
		}
		else if(nfds == 1)
		{
			if(rev.events & (EPOLLIN|EPOLLPRI))
			{
				long long r = 0;
				read(event_fd, &r, sizeof(r));
				printf("- read eventfd, get:%lld\n- call read again", r);
				read(event_fd, &r, sizeof(r));
				printf("get:%lld\ncall read again", r);
			}
		}
	}
	return NULL;
}
TEST(Evnetfd, Performance)
{
	pthread_t thread_id;
	int count=100000;
	event_fd = eventfd(0, 0);

	ASSERT_EQ(0, pthread_create(&thread_id, NULL, envntfd_thread, NULL));
	long long delta=2;
	sleep(2);
	struct timeval start;
	struct timeval end;
	float timeelapse = 0;
	gettimeofday(&start, NULL);
	for(int i=0;i<count;i++)
	{
		//printf("+ send SIGUSR1\n");
		write(event_fd, &delta, sizeof(delta));
	}
	gettimeofday(&end, NULL);
	timeelapse = (end.tv_sec - start.tv_sec) * 1000000
                  + (end.tv_usec - start.tv_usec);
	printf("+ write eventfd %d times, elapsed %f usec\n", count, timeelapse);
	fflush(stdout);
	sleep(5);
}
