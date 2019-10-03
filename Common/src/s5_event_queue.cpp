#include "s5_utils.h"
#include "s5_event_queue.h"
S5EventQueue::S5EventQueue()
{
	current_queue = NULL;
}
int S5EventQueue::init(const char* name, int size, BOOL semaphore_mode)
{
	pthread_spin_init(&lock, 0);
	safe_strcpy(this->name, name, sizeof(this->name));
	event_fd = eventfd(0, EFD_SEMAPHORE);
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
	AutoSpinLock(lock);
	return current_queue->enqueue(S5Event{ type, arg_i, arg_p });
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
int S5EventQueue::get_event(fixed_size_queue** /*out*/ q)
{
	int64_t v;
	if( unlikely(read(event_fd, &v, sizeof(v)) != sizeof(v)))
	{
		S5LOG_ERROR("Failed read event fd, rc:%d", -errno);
		return -errno;
	}
	AutoSpinLock(lock);
	*q = current_queue;
	current_queue = current_queue == &queue1 ? &queue2 : &queue1;
	return 0;
}
