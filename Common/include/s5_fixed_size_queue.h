#ifndef _FIXED_SIZE_QUEUE_H_
#define _FIXED_SIZE_QUEUE_H_

#include <stdlib.h>
#include <errno.h>
#include "s5_utils.h"

/**
 * Copyright (C), 2014-2019.
 * @file
 * A simple, fixed-size, and  non-thread-safe queue.
 *
 * This is a simple queue, implemented with array and header&tail pointer.It is not thread-safe.
 * User needs to make sure thread safety.
 *
 * @author xxx
 */

/**
 * Get the available space in queue.
 *
 * @param[in]	size	queue depth
 * @param[in]	head	head position of queue
 * @param[in]	tail	tail position of queue
 *
 * @return available space in queue.
 */
#define QUEUE_SPACE(size, head, tail) 	 ( (head) <=  (tail) ?   (size - tail + head - 1) : (head - tail - 1) )

/**
 * get the valid entries count in queue
 *
 * @param[in]	size	queue depth
 * @param[in]	head	head position of queue
 * @param[in]	tail	tail position of queue
 *
 * @return valid element number in queue.
 */
#define QUEUE_COUNT(size, head, tail) ( (head) <= (tail) ? (tail-head) : (size + tail - head) )



/**
 * Data structure of fixed-size queue.
 */
template <typename T>
class S5FixedSizeQueue
{
public:
	int tail;			///< tail pointer
	int head;			///< head pointer
	int queue_depth;	///< queue depth, max number of element can be put to this queue plus 1
	T* data;		///< memory used by this queue, it's size of queue_depth * ele_size
//	char name[32];

	S5FixedSizeQueue():tail(0),head(0),queue_depth(0),data(NULL)
	{ }
	~S5FixedSizeQueue()
	{
		destroy();
	}
/**
 * Initialize a queue.
 *
 * The queue depth conforms to Depth = 2 ^ queue_depth_order - 1.
 * For example, with queue_depth_order=10, this queue can contains 1023 elements.
 * because this queue is implemented with an array which size is of 2 ^ queue_depth_order
 * and the queue can contain array_size - 1 elements.
 *
 * @param[in,out]	queue				queue to initialize
 * @param[in]		queue_depth_order	queue depth order value
 * @return 0 on success, -ENOMEM on fail
 */
int init(int _queue_depth)
{
	head = tail = 0;
	this->queue_depth = _queue_depth+1;
	data = (T*)calloc((size_t)queue_depth, sizeof(T));
	if (data == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * Destroy an unused queue.
 */
void destroy()
{
	queue_depth = tail = head = 0;
	free(data);
	data = NULL;
}

/**
 * Enqueue an element to the tail of queue.
 *
 * Although pointer to element is passed, the queue store the element by value, not pointer itself.
 * So user needs free this element if it is dynamically allocated and will not be used anymore.
 *
 * @param[in]	queue		the queue to operate on
 * @param[in]	element		the content of element, i.e. element_size bytes, will be copied to queue
 * @return 0 on success and negative for errors.
 * @retval		-EAGAIN		Out of resources. the queue is full
 *				0			Success.
 */
inline int enqueue(/*in*/const T& element)
{
	if (is_full())
		return -EAGAIN;
	data[tail] = element;
	tail = (tail + 1)%queue_depth;
	return 0;
}

/**
 * Dequeue an element from the head of the queue.
 *
 * User cannot long term retain element dequeued, and should not free it also.
 *
 * @param[in]	queue	the queue to operate on
 * @return a T
 * @throws bad_logic exception if queue is empty.
 *
 * @mark, dequeue must return a value of T, not T* or T&. T* or T& is a pointer to data[i],
 * after dequeue return, data[i] may has changed its value by other call to enqueue before
 * caller consume dequeue's return value
 */
inline T dequeue()
{
	if (unlikely(is_empty()))
		throw std::logic_error(format_string("queue:%s is empty", "noname"));
	T t = data[head];
	head = (head+1)% queue_depth;
	return t;
}
/**
 * Returns whether the queue is empty(i.e. whether its size is 0).
 *
 * @param[in]	queue	queue depth
 *
 * @retval		1		queue is empty
 * @retval		0		queue is not empty
 */
inline bool is_empty() { return tail == head; }
/**
 * Returns whether the queue container is full(i.e. whether its remaining space is 0).
 *
 * @param[in]	queue	queue depth
 *
 * @retval		1		queue is full
 * @retval		0		queue is not full
 */
inline bool is_full() {	return QUEUE_SPACE(queue_depth, head, tail) == 0; }

/**
 * Get the available space in queue.
 *
 * @param[in]	queue	queue to compute
 *
 * @return available space in queue.
 */
inline int space() { 	return QUEUE_SPACE(queue_depth, head, tail); }

/**
 * get the valid entries count in queue
 *
 * @param[in]	queue	queue to compute
 *
 * @return valid element number in queue.
 */
inline int count() {	return QUEUE_COUNT(queue_depth, head, tail); }
} ;




#endif //_FIXED_SIZE_QUEUE_H_
