#ifndef s5_event_queue_h__
#define s5_event_queue_h__
#include <pthread.h>
#include "s5_fixed_size_queue.h"
struct S5Event
{
	int type;
	int arg_i;
	void* arg_p;

};
class S5EventQueue
{
public:
	char name[32];
	//ping-bong queue, to accelerate retrive speed
	S5FixedSizeQueue<S5Event> queue1;
	S5FixedSizeQueue<S5Event> queue2;
	S5FixedSizeQueue<S5Event>* current_queue;
	pthread_spin_lock_t lock;
	int event_fd;

	S5EventQueue();
	~S5EventQueue();

	int init(const char* name, int size, BOOL semaphore_mode);
	void destroy();
	int post_event(int type, int arg_i, void* arg_p);
	int get_events(S5FixedSizeQueue<S5Event>** /*out*/ q);
	int get_event(S5Event* /*out*/ evt);
};

#endif // s5_event_queue_h__
