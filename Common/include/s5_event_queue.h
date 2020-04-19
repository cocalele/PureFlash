#ifndef s5_event_queue_h__
#define s5_event_queue_h__
#include <pthread.h>
#include <functional>
#include <semaphore.h> //for sem_t

#include "s5_fixed_size_queue.h"
struct S5Event
{
	int type;
	int arg_i;
	void* arg_p;

};
enum S5EventType : int
{
	EVT_SYNC_INVOKE=1,
	EVT_EPCTL_DEL,
	EVT_EPCTL_ADD,
	EVT_IO_REQ,
	EVT_IO_COMPLETE,
	EVT_IO_TIMEOUT,
	EVT_REOPEN_VOLUME,
	EVT_VOLUME_RECONNECT,
	EVT_SEND_HEARTBEAT,
	EVT_THREAD_EXIT,
};

class S5EventQueue
{
public:
	char name[32];
	//ping-bong queue, to accelerate retrieve speed
	S5FixedSizeQueue<S5Event> queue1;
	S5FixedSizeQueue<S5Event> queue2;
	S5FixedSizeQueue<S5Event>* current_queue;
	pthread_spinlock_t lock;
	int event_fd;

	S5EventQueue();
	~S5EventQueue();

	int init(const char* name, int size, BOOL semaphore_mode);
	void destroy();
	int post_event(int type, int arg_i, void* arg_p);
	int get_events(S5FixedSizeQueue<S5Event>** /*out*/ q);
	int get_event(S5Event* /*out*/ evt);
	inline bool is_empty() { return current_queue->is_empty();}
	int sync_invoke(std::function<int()> f);
};

struct SyncInvokeArg
{
        sem_t sem;
        int rc;
        std::function<int()> func;
};


#endif // s5_event_queue_h__
