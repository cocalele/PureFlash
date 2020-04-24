#include <sys/eventfd.h>
#include <unistd.h>

#include "pf_utils.h"
#include "pf_event_queue.h"
#include "pf_lock.h"

static int64_t event_delta = 1;
S5EventQueue::S5EventQueue():event_fd(0)
{
	current_queue = NULL;
}
int S5EventQueue::init(const char* name, int size, BOOL semaphore_mode)
{
	pthread_spin_init(&lock, 0);
	safe_strcpy(this->name, name, sizeof(this->name));
	event_fd = eventfd(0, semaphore_mode ? EFD_SEMAPHORE:0);
	if (event_fd < 0)
	{
		S5LOG_ERROR("Failed create eventfd for EventQueue:%s, rc:%d", name, -errno);
		return -errno;
	}
	int rc = 0;
	rc = queue1.init(size);
	if(rc)
	{
		S5LOG_ERROR("Failed init event queue for EventQueue:%s, rc:%d", name, rc);
		goto release1;
	}
	rc = queue2.init(size);
	if (rc)
	{
		S5LOG_ERROR("Failed init event queue for EventQueue:%s, rc:%d", name, rc);
		goto release2;
	}
	current_queue = &queue1;
	return 0;
release2:
	queue1.destroy();
release1:
	close(event_fd);
	return rc;
}
S5EventQueue::~S5EventQueue()
{
	if (current_queue)
		destroy();
}

void S5EventQueue::destroy()
{
	current_queue = NULL;
	queue1.destroy();
	queue1.destroy();
	close(event_fd);
}

int S5EventQueue::post_event(int type, int arg_i, void* arg_p)
{
	AutoSpinLock _l(&lock);
	int rc = current_queue->enqueue(S5Event{ type, arg_i, arg_p });
	if(rc)
		return rc;
	write(event_fd, &event_delta, sizeof(event_delta)); 
	return 0;
	//if (current_queue->is_full())
	//	return -EAGAIN;
	//current_queue->data[current_queue->tail] = S5Event{ type, arg_i, arg_p };
	//current_queue->tail = (current_queue->tail + 1) % current_queue->queue_depth;
}

/**
 * get events in queue. events are fetched via _@param q_. q may be empty if there
 * are no events.
 * caller can iterate over @param q to process each event.
 *
 * This function may block caller if there's no event in queue. But for some case
 * it may not block and return an empty q.
 *
 * @param[out] q, events in the queue.
 * @return 0 on success, negative code on error.
 *
 */
int S5EventQueue::get_events(S5FixedSizeQueue<S5Event>** /*out*/ q)
{
	int64_t v;
	if( unlikely(read(event_fd, &v, sizeof(v)) != sizeof(v)))
	{
		S5LOG_ERROR("Failed read event fd, rc:%d", -errno);
		return -errno;
	}
	AutoSpinLock _l(&lock);
	*q = current_queue;
	current_queue = current_queue == &queue1 ? &queue2 : &queue1;
	return 0;
}

int S5EventQueue::get_event(S5Event* /*out*/ evt)
{
	int64_t v;
	if( unlikely(read(event_fd, &v, sizeof(v)) != sizeof(v)))
	{
		S5LOG_ERROR("Failed read event fd, rc:%d", -errno);
		return -errno;
	}
	AutoSpinLock _l(&lock);
	if(current_queue->is_empty())
		return -ENOENT;
	*evt = current_queue->data[current_queue->head];
	current_queue->head++;
	current_queue->head %= current_queue->queue_depth;
	return 0;
}

int S5EventQueue::sync_invoke(std::function<int()> f)
{
	SyncInvokeArg  arg;
	arg.func = f;
	sem_init(&arg.sem, 0, 0);
	int rc = post_event(EVT_SYNC_INVOKE, 0, &arg);
	if (rc)
	{
		sem_destroy(&arg.sem);
		S5LOG_ERROR("Failed post EVT_SYNC_INVOKE event");
		return rc;
	}
	sem_wait(&arg.sem);
	sem_destroy(&arg.sem);
	return arg.rc;
}
