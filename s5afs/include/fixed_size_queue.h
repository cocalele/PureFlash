#ifndef _FIXED_SIZE_QUEUE_H_
#define _FIXED_SIZE_QUEUE_H_


/**
 * Copyright (C), 2014-2015.
 * @file
 * A simple, fixed-size, and  non-thread-safe queue.
 *
 * This is a simple queue, implemented with array and header&tail pointer.It is not thread-safe.
 * User needs to make sure thread safety.
 *
 * @author xxx
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Data structure of fixed-size queue.
 */
typedef struct fixed_size_queue
{
	int tail;			///< tail pointer
	int head;			///< head pointer
	int queue_depth;	///< queue depth, max number of element can be put to this queue plus 1
	void** data;		///< memory used by this queue, it's size of queue_depth * ele_size
	int index_mask;		///< mask for head and tail index, to make value valid
} fixed_size_queue_t;

/**
 * fixed size queue, but store data type int, not void*
 * fixed_size_queue can be treat as fixed_size_queue<void*> in C++
 * fixed_size_queue_int can be tread as fixed_size_queue<int> in C++
 */
typedef struct fixed_size_queue_int
{
	int tail;			///< tail pointer
	int head;			///< head pointer
	int queue_depth;	///< queue depth, max number of element can be put to this queue plus 1
	int* data;		///< memory used by this queue, it's size of queue_depth * ele_size
} fixed_size_queue_int_t;

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
 */
void fsq_init(fixed_size_queue_t *queue, int queue_depth_order);
int fsq_int_init(fixed_size_queue_int_t *queue, int queue_depth);

/**
 * Destroy an unused queue.
 */
void fsq_destory(fixed_size_queue_t *queue);
void fsq_int_destory(fixed_size_queue_int_t *queue);
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
int fsq_enqueue(fixed_size_queue_t *queue, /*in*/void* element);
int fsq_int_enqueue(fixed_size_queue_int_t *queue, /*in*/int element);

/**
 * Dequeue an element from the head of the queue.
 *
 * User cannot long term retain element dequeued, and should not free it also.
 *
 * @param[in]	queue	the queue to operate on
 * @return an s5msg_queue_item, or else NULL if the queue is empty.
 */
void* fsq_dequeue(fixed_size_queue_t *queue);
int fsq_int_dequeue(fixed_size_queue_int_t *queue);

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
 * Returns whether the queue container is empty(i.e. whether its size is 0).
 *
 * @param[in]	queue	queue depth
 *
 * @retval		1		queue is empty
 * @retval		0		queue is not empty
 */
#define fsq_is_empty(queue) ( (queue)->tail == (queue)->head )

/**
 * Returns whether the queue container is full(i.e. whether its remaining space is 0).
 *
 * @param[in]	queue	queue depth
 *
 * @retval		1		queue is full
 * @retval		0		queue is not full
 */
#define fsq_is_full(queue) ( QUEUE_SPACE((queue)->queue_depth, (queue)->head, (queue)->tail) == 0 )

/**
 * Get the available space in queue.
 *
 * @param[in]	queue	queue to compute
 *
 * @return available space in queue.
 */
#define fsq_space(queue) ( QUEUE_SPACE((queue)->queue_depth, (queue)->head, (queue)->tail) )

/**
 * get the valid entries count in queue
 *
 * @param[in]	queue	queue to compute
 *
 * @return valid element number in queue.
 */
#define fsq_count(queue) ( QUEUE_COUNT((queue)->queue_depth, (queue)->head, (queue)->tail) )

#ifdef __cplusplus
}
#endif

#endif //_FIXED_SIZE_QUEUE_H_