#include <sys/prctl.h>
#include "s5_event_thread.h"
void *EventThread::start_routine(void* arg)
{
	prctl(PR_SET_NAME, buf);
	EventThread* pThis = (EventThread*)arg;
	fixed_size_queue* q;
	int rc = 0;
	while ((rc = pThis->event_queue.get_event(&q)) == 0)
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

