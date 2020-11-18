#ifndef pf_event_queue_h__
#define pf_event_queue_h__
#include <pthread.h>
#include <functional>
#include <semaphore.h> //for sem_t

#include "pf_fixed_size_queue.h"
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
	EVT_RECV_REQ,
	EVT_SEND_REQ,
	EVT_COW_READ,
	EVT_COW_WRITE,
	EVT_RECOVERY_READ_IO,
	EVT_CONN_CLOSED,
};
const char* EventTypeToStr(S5EventType t);

class PfEventQueue
{
public:
	char name[32];
	//ping-bong queue, to accelerate retrieve speed
	PfFixedSizeQueue<S5Event> queue1;
	PfFixedSizeQueue<S5Event> queue2;
	PfFixedSizeQueue<S5Event>* current_queue;
	pthread_spinlock_t lock;
	int event_fd;

	PfEventQueue();
	~PfEventQueue();

	int init(const char* name, int size, BOOL semaphore_mode);
	void destroy();
	int post_event(int type, int arg_i, void* arg_p);
	int get_events(PfFixedSizeQueue<S5Event>** /*out*/ q);
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


#endif // pf_event_queue_h__
