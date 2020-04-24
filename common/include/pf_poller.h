#ifndef pf_poller_h__
#define pf_poller_h__
#include <stdint.h>
#include <sys/epoll.h>

#include "pf_event_queue.h"
#include "pf_mempool.h"

typedef void (*epoll_evt_handler)(int fd, uint32_t event, void* user_arg);
struct PollerFd
{
	int fd;
	struct epoll_event events_to_watch;
	epoll_evt_handler handler;
	void* cbk_arg;
};
class S5Poller
{
public:
	int epfd;
	struct S5EventQueue ctrl_queue;
	ObjectMemoryPool<PollerFd> desc_pool;
	pthread_t tid;
	char name[32];
	int max_fd;

	S5Poller();
	~S5Poller();
	int init(const char* name, int max_fd_count);
	int add_fd(int fd, uint32_t events, epoll_evt_handler callback, void* callback_data);
	int del_fd(int fd);
	void destroy();
	void run();
	static void* thread_entry(void* arg);
private:
	int async_add_fd(int fd, uint32_t events, epoll_evt_handler callback, void* callback_data);
	int async_del_fd(int fd);
};

#endif // pf_poller_h__
