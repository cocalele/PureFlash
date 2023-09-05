#ifndef pf_event_thread_h__
#define pf_event_thread_h__
#include <functional>
#include "pf_event_queue.h"

class PfEventThread
{
public:
	pfqueue *event_queue;
	pthread_t tid;
	char name[32];
	int (*func_priv)(int *, void *);
	void *arg_v;


	bool inited;
	bool exiting=false;
	int init(const char* name, int queue_depth);
	PfEventThread();
	void destroy();
	virtual ~PfEventThread();
	virtual int process_event(int event_type, int arg_i, void* arg_p, void* arg_q) = 0;
	int start();
	void stop();
	void * (*thread_proc)(void* arg);

	int sync_invoke(std::function<int(void)> _f);
	virtual int commit_batch(){return 0;};
};
#endif // pf_event_thread_h__
