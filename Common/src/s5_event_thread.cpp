#include <sys/prctl.h>
#include "s5_event_thread.h"

S5EventThread::~S5EventThread()
{

}
int S5EventThread::start()
{
	int rc = pthread_create(&tid, NULL, thread_proc, this);
	if(rc)
	{
		S5LOG_ERROR("Failed create thread:%s, rc:%d", name, rc);
		return rc;
	}
	return 0;
}
void S5EventThread::stop()
{
	event_queue.post_event(EVT_THREAD_EXIT, 0, NULL);
	pthread_join(tid, NULL);

}

void *S5EventThread::thread_proc(void* arg)
{
	S5EventThread* pThis = (S5EventThread*)arg;
	prctl(PR_SET_NAME, pThis->name);
	S5FixedSizeQueue<S5Event>* q;
	int rc = 0;
	while ((rc = pThis->event_queue.get_events(&q)) == 0)
	{
		while(!q->is_empty())
		{
			S5Event* t = &q->data[q->head];
			q->head = (q->head + 1) % q->queue_depth;
			pThis->process_event(t->type, t->arg_i, t->arg_p);
		}
	}
	return NULL;
}

int S5EventThread::sync_invoke(std::function<int(void)> _f)
{
	S5LOG_FATAL("sync_invoke not implemented");
	return 0;
}

