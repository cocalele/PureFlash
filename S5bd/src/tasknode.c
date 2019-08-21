#include "tasknode.h"

void s5_unitnode_reset(s5_unitnode_t *unode)
{
	unode->task_id = -1;
	unode->flag = NODE_IDLE;
	unode->nlba = 0;
	unode->len = 0;
	unode->ofs = 0;
	unode->comp = NULL;
	unode->ictx = NULL;
	unode->readbuf = NULL;
}

void s5_unitnode_queue_init(s5_unitnode_queue_t* queue)
{
	queue->head = NULL;
	queue->tail = NULL;
	queue->length = 0;
}

void s5_unitnode_queue_release(s5_unitnode_queue_t* queue)
{
}

s5_unitnode_t* s5_unitnode_queue_head(s5_unitnode_queue_t* queue)
{
	return queue->head;
}

s5_unitnode_t* s5_unitnode_queue_tail(s5_unitnode_queue_t* queue)
{
	return queue->tail;
}

int s5_unitnode_queue_length(s5_unitnode_queue_t* queue)
{
	return queue->length;
}

BOOL s5_unitnode_queue_empty(s5_unitnode_queue_t* queue)
{
	if(queue->length == 0)
		return TRUE;
	else
		return FALSE;
}

void s5_unitnode_queue_enqueue(s5_unitnode_queue_t* queue, s5_unitnode_t* unode)
{
	unode->next = NULL;
	if(s5_unitnode_queue_empty(queue))
	{
		queue->tail = unode;
		queue->head = queue->tail;
	}
	else
	{
		queue->tail->next = unode;
		queue->tail = unode;
	}
	++queue->length;

	return;
}

s5_unitnode_t* s5_unitnode_queue_dequeue(s5_unitnode_queue_t* queue)
{
	s5_unitnode_t* ret = NULL;
	if(s5_unitnode_queue_empty(queue))
	{
		return NULL;
	}
	ret = queue->head;
	queue->head = queue->head->next;
	--queue->length;
	ret->next = NULL;

	return ret;
}


void s5_blocknode_init(s5_blocknode_t* bnode)
{
	bnode->flag = NODE_IDLE;
	bnode->running_num = 0;
	s5_unitnode_queue_init(&bnode->readyqueue);
	bitarray_init(&bnode->barr, SLOT_SIZE);
}

void s5_blocknode_release(s5_blocknode_t* bnode)
{
	bitarray_release(bnode->barr);
	s5_unitnode_queue_release(&bnode->readyqueue);
}

