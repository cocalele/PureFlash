#ifndef pf_event_thread_h__
#define pf_event_thread_h__
#include <functional>
#include "pf_event_queue.h"

struct pf_thread_stats {
	uint64_t busy_tsc;
	uint64_t idle_tsc;
};

/**
 * Pollers should always return a value of this type
 * indicating whether they did real work or not.
 */
enum pf_thread_poller_rc {
	PF_POLLER_IDLE,
	PF_POLLER_BUSY,
};

class PfEventThread
{
public:
	pfqueue *event_queue;
	pthread_t tid;
	char name[32];
	uint16_t poller_id;
	uint32_t proceessed_events;
	uint64_t tsc_rate;
	uint64_t tsc_last;
	pf_thread_stats stats;

	int (*func_priv)(int *, void *) = NULL;
	void *arg_v;

	bool inited;
	bool exiting = false;
	int init(const char* name, int queue_depth, uint16_t p_id);
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

typedef void (*pf_event_fn)(void *ctx);

struct thread_stat {
	std::string  name;
	pthread_t tid;
	pf_thread_stats stats;
};

struct get_stats_ctx {
	pf_event_fn fn;
	int next_thread_id;
	int num_threads;
	std::vector<pfqueue*> threads;
	std::vector<thread_stat> thread_stats;
	uint64_t now;
};

int get_thread_stats(pf_thread_stats *stats);
PfEventThread * get_current_thread();
#endif // pf_event_thread_h__
