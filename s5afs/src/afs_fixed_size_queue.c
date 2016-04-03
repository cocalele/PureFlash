#include <stdlib.h>
#include <errno.h>
#include "fixed_size_queue.h"

void fsq_init(fixed_size_queue_t *queue, int queue_depth_order)
{
	queue->head = queue->tail = 0;
	queue->queue_depth = 1<<queue_depth_order;
	queue->index_mask = queue->queue_depth-1;
	queue->data=(void**)malloc((size_t)queue->queue_depth*sizeof(void*));
}
int fsq_int_init(fixed_size_queue_int_t *queue, int queue_depth)
{
	queue->head = queue->tail = 0;
	queue->queue_depth = queue_depth;
	queue->data = (int*)malloc((size_t)queue->queue_depth*sizeof(int));
	if (queue->data == NULL)
		return -ENOMEM;
	return 0;
}

void fsq_destory(fixed_size_queue_t *queue)
{
	free(queue->data);
}
void fsq_int_destory(fixed_size_queue_int_t *queue)
{
	free(queue->data);
}

int fsq_enqueue(fixed_size_queue_t *queue, void* element)
{
	if(fsq_is_full(queue))
		return -EAGAIN;
	queue->data[queue->tail] = element;
	queue->tail++;
	queue->tail &= queue->index_mask;
	return 0;
}

int fsq_int_enqueue(fixed_size_queue_int_t *queue, int element)
{
	if (fsq_is_full(queue))
		return -EAGAIN;
	queue->data[queue->tail] = element;
	queue->tail++;
	queue->tail %= queue->queue_depth;
	return 0;
}


void* fsq_dequeue(fixed_size_queue_t *queue)
{
	if(fsq_is_empty(queue))
		return NULL;
	void* t = queue->data[queue->head];
	queue->head ++;
	queue->head &= queue->index_mask;
	return t;
}

int fsq_int_dequeue(fixed_size_queue_int_t *queue)
{
	if (fsq_is_empty(queue))
		return -1;
	int t = queue->data[queue->head];
	queue->head++;
	queue->head %= queue->queue_depth;;
	return t;
}

