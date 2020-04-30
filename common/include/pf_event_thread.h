#ifndef pf_event_thread_h__
#define pf_event_thread_h__
#include <functional>
#include "pf_event_queue.h"

class PfEventThread
{
public:
	PfEventQueue event_queue;
	pthread_t tid;
	char name[32];

	bool inited;
	int init(const char* name, int queue_depth);
	PfEventThread();
	void destroy();
	virtual ~PfEventThread();
	virtual int process_event(int event_type, int arg_i, void* arg_p) = 0;
	int start();
	void stop();
	static void *thread_proc(void* arg);

	int sync_invoke(std::function<int(void)> _f);

};
#endif // pf_event_thread_h__
