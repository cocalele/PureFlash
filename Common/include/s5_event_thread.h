#ifndef s5_event_thread_h__
#define s5_event_thread_h__
#include <functional>
#include "s5_event_queue.h"

class S5EventThread
{
public:
	S5EventQueue event_queue;
	pthread_t tid;
	char name[32];

	virtual ~S5EventThread();
	virtual int process_event(int event_type, int arg_i, void* arg_p) = 0;
	int start();

	static void *thread_proc(void* arg);

	int sync_invoke(std::function<int(void)> _f);

};
#endif // s5_event_thread_h__
