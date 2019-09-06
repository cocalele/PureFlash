#ifndef s5_event_queue_h__
#define s5_event_queue_h__
#include "s5_fixed_size_queue.h"
struct S5Event
{
	int type;
	int arg_i;
	void* arg_p;

};
class EventQueue
{
	char name[32];
	//ping-bong queue, to accelerate retrive speed
	fixed_size_queue<S5Event> queue1;
	fixed_size_queue<S5Event> queue2;
	fixed_size_queue* current_queue;
	pthread_spin_lock lock;
	int event_fd;

	EventQueue(const char* name);
	~EventQueue();

	int init(int size)
	int post_event(int type, int arg_i, void* arg_p);
	int EventQueue::get_event(fixed_size_queue** /*out*/ q);
};

#endif // s5_event_queue_h__
