#include <sys/prctl.h>
#include <string.h>

#include "pf_event_thread.h"
PfEventThread::PfEventThread() {
	inited = false;
}
int PfEventThread::init(const char* name, int qd)
{
	int rc = event_queue.init(name, qd, 0);
	if(rc)
		return rc;
	strncpy(this->name, name, sizeof(this->name));
	inited = true;
	return 0;
}
void PfEventThread::destroy()
{
	if(inited) {
		if(tid)
			stop();
		event_queue.destroy();
		inited = false;
	}
}
PfEventThread::~PfEventThread()
{
	destroy();
}

int PfEventThread::start()
{
	int rc = pthread_create(&tid, NULL, thread_proc, this);
	if(rc)
	{
		S5LOG_ERROR("Failed create thread:%s, rc:%d", name, rc);
		return rc;
	}
	return 0;
}
void PfEventThread::stop()
{
	event_queue.post_event(EVT_THREAD_EXIT, 0, NULL);
	int rc = pthread_join(tid, NULL);
	if(rc) {
		S5LOG_ERROR("Failed call pthread_join on thread:%s, rc:%d", name, rc);
	}
	tid=0;

}

void *PfEventThread::thread_proc(void* arg)
{
	PfEventThread* pThis = (PfEventThread*)arg;
	prctl(PR_SET_NAME, pThis->name);
	PfFixedSizeQueue<S5Event>* q;
	int rc = 0;
	while ((rc = pThis->event_queue.get_events(&q)) == 0)
	{
		while(!q->is_empty())
		{
			S5Event* t = &q->data[q->head];
			q->head = (q->head + 1) % q->queue_depth;
			switch(t->type){
				case EVT_SYNC_INVOKE:
				{
					SyncInvokeArg* arg = (SyncInvokeArg*)t->arg_p;
					arg->rc = arg->func();
					sem_post(&arg->sem);
					break;
				}
				case EVT_THREAD_EXIT:
				{
					S5LOG_INFO("exit thread:%s", pThis->name);
					return NULL;
				}
				default:
					pThis->process_event(t->type, t->arg_i, t->arg_p, t->arg_q);
			}
		}
		pThis->commit_batch();
	}
	return NULL;
}


int PfEventThread::sync_invoke(std::function<int(void)> _f)
{
	SyncInvokeArg arg;
	sem_init(&arg.sem, 0, 0);
	arg.func = std::move(_f);
	this->event_queue.post_event(EVT_SYNC_INVOKE, 0, &arg);
	sem_wait(&arg.sem);
	return arg.rc;
}

