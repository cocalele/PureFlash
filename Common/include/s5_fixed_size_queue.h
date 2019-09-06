#ifndef _FIXED_SIZE_QUEUE_H_
#define _FIXED_SIZE_QUEUE_H_


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
class fixed_size_queue
{
public:
	int tail;			///< tail pointer
	int head;			///< head pointer
	int queue_depth;	///< queue depth, max number of element can be put to this queue plus 1
	T* data;		///< memory used by this queue, it's size of queue_depth * ele_size
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
int init(int queue_depth)
{
	head = queue->tail = 0;
	this->queue_depth = queue_depth;
	data = (T*)calloc((size_t)queue->queue_depth, sizeof(T));
	if (data == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * Destroy an unused queue.
 */
void destroy()
{
	free(data);
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
	if (fsq_is_full(this))
		return -EAGAIN;
	data[queue->tail] = element;
	tail = (tail + 1)%queue_depth;
	return 0;
}

/**
 * Dequeue an element from the head of the queue.
 *
 * User cannot long term retain element dequeued, and should not free it also.
 *
 * @param[in]	queue	the queue to operate on
 * @return a T, or else NULL if the queue is empty.
 */
inline const T* dequeue()
{
	if (unlikely(is_empty()))
		return NULL;
	T* t = &data[queue->head];
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
inline int space(queue) { 	return QUEUE_SPACE((queue)->queue_depth, (queue)->head, (queue)->tail); }

/**
 * get the valid entries count in queue
 *
 * @param[in]	queue	queue to compute
 *
 * @return valid element number in queue.
 */
inline int count(queue) {	return QUEUE_COUNT((queue)->queue_depth, (queue)->head, (queue)->tail); }
} ;




#endif //_FIXED_SIZE_QUEUE_H_