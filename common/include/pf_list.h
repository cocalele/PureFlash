#ifndef __S5LIST__
#define __S5LIST__

/**
* Copyright (C), 2014-2015.
* @file
* s5list C API.
*
* This file includes all s5list data structures and APIs.
*/

#ifdef __cplusplus
extern "C" {
#endif
#include <pthread.h>
#include <linux/types.h>

#define LIST_POISON1  ((void *) 0x00100100)	///< Default value1 of list's position.
#define LIST_POISON2  ((void *) 0x00200200)	///< Default value2 of list's position.

/**
 * s5 dlist entry declare.
 */
struct pf_dlist_entry;

/**
 * s5 dlist head.
 */
typedef struct pf_dlist_head
{
	struct pf_dlist_entry       *list;	///< the first entry.
	pthread_mutex_t 			lock;	///< lock to protect list entries.
	pthread_cond_t              cond;	///< cond to control list together lock.
	int                         count;	///< count of list entries.
} pf_dlist_head_t, *ppf_dlist_head_t;

/**
 *  s5 dlist entry.
 */
typedef struct pf_dlist_entry
{
	struct pf_dlist_entry *prev;	///< pointer to prev entry.
	struct pf_dlist_entry *next;	///< pointer to next entry.
	struct pf_dlist_head *head;		///< pointer to list head.
	void*	param;		///< pointer to parameter recorded in entry.
} pf_dlist_entry_t, *ppf_dlist_entry_t;

/**
 * init s5 dlist head.
 *
 * @param[in] head	s5 dlist head.
 * @return 	0 			on success, negative error code on failure.
 * @retval	0			success.
 * @retval     -EINVAL		head is invalid.
 */
int s5list_init_head(ppf_dlist_head_t head);

/**
 *  lock the list.
 * @param[in] head	s5 dlist head.
 * @return 	0 			on success, error code on failure.
 * @retval	0			success.
 * @retval     EINVAL The value specified by mutex refer to head does not refer to an initialized mutex object.
 * @retval     EAGAIN The mutex refer to head could not be acquired because the maximum number of recursive locks for mutex has been exceeded.
 * @retval     EDEADLK	The current thread already owns the mutex refer to head.
 */
#define s5list_lock(head)    pthread_mutex_lock(&((ppf_dlist_head_t)head)->lock)

/**
 *  try lock the list, if the lock is currently locked, return immediately.
 *
 * @param[in] head	5 dlist head.
 * @return 	0 			on success, error code on failure.
 * @retval	0			success.
 * @retval     EBUSY  The mutex could not be acquired because it was already locked.
 * @retval     EINVAL The value specified by mutex does not refer to an initialized mutex object.
 * @retval     EAGAIN The mutex could not be acquired because the maximum number of recursive locks for mutex has been exceeded.
 * @retval     EDEADLK	The current thread already owns the mutex.
 */
#define s5list_trylock(head)    pthread_mutex_trylock(&((ppf_dlist_head_t)head)->lock)

/**
 *  unlock the list.
 *
 * @param[in] head	s5 dlist head.
 * @return 	0 			on success, error code on failure.
 * @retval	0			success.
 * @retval     EINVAL The value specified by mutex refer to head does not refer to an initialized mutex object.
 * @retval     EAGAIN The mutex refer to head could not be acquired because the maximum number of recursive locks for mutex has been exceeded.
 * @retval     EDEADLK	The current thread already owns the mutex refer to head.
 * @retval     EPERM  The current thread does not own the mutex refer to head.
 */
#define s5list_unlock(head)    pthread_mutex_unlock(&((ppf_dlist_head_t)head)->lock)

/**
 * s5 dlist wait entry.
 *
 * @param[in] head	s5 dlist head.
 * @return 	0 			on success, error code on failure.
 * @retval	0			success.
 * @retval     EINVAL The value specified by cond, mutex refer to head is invalid.
 * @retval     EINVAL Different  mutexes refer to head  were  supplied  for concurrent s5list_wait_entry() operations on the  same condition variable.
 * @retval     EAGAIN The mutex refer to head could not be acquired because the maximum number of recursive locks for mutex has been exceeded.
 * @retval     EPERM  The mutex was not owned by the current thread at the time of the call.
 */
#define s5list_wait_entry(head)		pthread_cond_wait(&((ppf_dlist_head_t)head)->cond, &((ppf_dlist_head_t)head)->lock)

/**
* s5 dlist wait entry timeout.
*
* @param[in] head	s5 dlist head.
* @param[in] outtime	timeout.
* @return  0		   on success, error code on failure.
* @retval   0		   success.
* @retval 	  ETIMEDOUT  The time specified by abstime to s5list_wait_entry_timeout() has passed.
* @retval	  EINVAL The value specified by cond, mutex refer to head is invalid.
* @retval	  EINVAL Different	mutexes refer to head  were  supplied  for concurrent s5list_wait_entry_timeout() operations on the  same condition variable.
* @retval	  EAGAIN The mutex refer to head could not be acquired because the maximum number of recursive locks for mutex has been exceeded.
* @retval	  EPERM  The mutex was not owned by the current thread at the time of the call.
*/
#define s5list_wait_entry_timeout(head, outtime)	pthread_cond_timedwait(&((ppf_dlist_head_t)head)->cond, &((ppf_dlist_head_t)head)->lock, outtime)



/**
 * signal s5 dlist have entry can get.
 *
 * @param[in] head	s5 dlist head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL The value cond refer to head does not refer to an initialized condition variable.
 */

#define s5list_signal_entry(head)		pthread_cond_signal(&((ppf_dlist_head_t)head)->cond)

/**
 *	operations of entry's member(param)
 */

/**
 * get s5 dlist entry's param.
 *
 * @param[in] entry	s5 dlist entry.
 * @return	pointer to param which is entry's field.
 */
#define s5list_get_entryparam(entry)			(entry)->param

/**
 * set s5 dlist entry's param.
 *
 * @param[in] entry		s5 dlist entry.
 * @param[in] parameter	param will to set.
 * @return	pointer to param which is entry's field.
 */
#define s5list_set_entryparam(entry, parameter)	(entry)->param = (parameter)


/**
 * add entry to list, insert from list-head.
 *
 * @param[in] new_entry	s5 dlist entry will to be pushed.
 * @param[in] head		s5 dlist head, push from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_push(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head);

/**
 * add entry to list, insert from list-tail.
 *
 * @param[in] new_entry	s5 dlist entry will to be pushed.
 * @param[in] head		s5 dlist head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_pushtail(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head);

/**
 * add entry to list, insert from list-head and have no lock.
 *
 * @param[in] new_entry	s5 dlist entry will to be pushed.
 * @param[in] head		s5 dlist head, push from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_push_ulc(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head);

/**
 * add entry to list, insert from list-tail and have no lock.
 *
 * @param[in] new_entry	s5 dlist entry will to be pushed.
 * @param[in] head		s5 dlist head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_pushtail_ulc(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head);

/**
 * insert entry to list before the given entry.
 *
 * @param[in] entry		s5 dlist entry given entry.
 * @param[in] new_entry	s5 dlist entry will to be inserted.
 * @param[in] head		s5 dlist head, push from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_insert_before( ppf_dlist_entry_t entry
                          , ppf_dlist_entry_t new_entry
                          , ppf_dlist_head_t head);

/**
 * insert entry to list after the given entry.
 *
 * @param[in] entry		s5 dlist entry given entry.
 * @param[in] new_entry	s5 dlist entry will to be inserted.
 * @param[in] head		s5 dlist head, push from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_insert_after( ppf_dlist_entry_t entry
                         , ppf_dlist_entry_t new_entry
                         , ppf_dlist_head_t head);

/**
 * insert entry to list before the given entry and have no lock.
 *
 * @param[in] entry		s5 dlist entry given entry.
 * @param[in] new_entry	s5 dlist entry will to be inserted.
 * @param[in] head		s5 dlist head, push from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_insert_before_ulc( ppf_dlist_entry_t entry
                              , ppf_dlist_entry_t new_entry
                              , ppf_dlist_head_t head);

/**
 * insert entry to list after the given entry and have no lock.
 *
 * @param[in] entry		s5 dlist entry given entry.
 * @param[in] new_entry	s5 dlist entry will to be inserted.
 * @param[in] head		s5 dlist head, push from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or new_entry or head is invalid.
 * @retval	EINVAL  new_entry had been located in other list.
 */
int s5list_insert_after_ulc( ppf_dlist_entry_t entry
                             , ppf_dlist_entry_t new_entry
                             , ppf_dlist_head_t head);


 /**
 * pop entry from list-head, the poped entry will be removed.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @return	non-NULL on success, NULL on failure.
 * @retval	non-NULL success.
 * @retval	NULL  	  head is NULL.	
 */

ppf_dlist_entry_t s5list_pop(ppf_dlist_head_t head);

/**
* pop entry from list-tail, the poped entry will be removed.
*
* @param[in] head		s5 dlist head, pop from the tail.
* @return  non-NULL on success, NULL on failure.
* @retval  non-NULL success.
* @retval  NULL 	 head is NULL.
*/
ppf_dlist_entry_t s5list_poptail(ppf_dlist_head_t head);

/**
* pop entry from list-head, the poped entry will be removed, and have no lock.
*
* @param[in] head		s5 dlist head, pop from the head.
* @return  non-NULL on success, NULL on failure.
* @retval  non-NULL success.
* @retval  NULL 	 head is NULL.
*/
ppf_dlist_entry_t s5list_pop_ulc(ppf_dlist_head_t head);

/**
* pop entry from list-tail, the poped entry will be removed, and have no lock.
*
* @param[in] head		s5 dlist head, pop from the tail.
* @return  non-NULL on success, NULL on failure.
* @retval  non-NULL success.
* @retval  NULL 	 head is NULL.
*/
ppf_dlist_entry_t s5list_poptail_ulc(ppf_dlist_head_t head);

/**
 * delete entry from list-head.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @param[in] entry		s5 dlist entry will be deleted.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	   EINVAL  entry or head is invalid.
 * @retval	   EINVAL  entry had been located in other list.
 */
int s5list_del_withh(ppf_dlist_head_t head, ppf_dlist_entry_t entry);

/**
 * delete entry from list-head and have no lock.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @param[in] entry		s5 dlist entry will be deleted.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or head is invalid.
 * @retval	EINVAL  entry had been located in other list.
 */
int s5list_del_withh_ulc(ppf_dlist_head_t head, ppf_dlist_entry_t entry);

/**
 * delete the given entry.
 *
 * @param[in] entry		s5 dlist entry will be deleted.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or entery's head is invalid.
 */
int s5list_del(ppf_dlist_entry_t entry);

/**
 * delete the given entry and have no lock.
 *
 * @param[in] entry		s5 dlist entry will be deleted.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  entry or entery's head is invalid.
 */
int s5list_del_ulc(ppf_dlist_entry_t entry);

/**
 * clear the list.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  	head is invalid.
 */
int s5list_clear(ppf_dlist_head_t head);

/**
 * clear the list and have no lock.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EINVAL  	head is invalid.
 */
int s5list_clear_ulc(ppf_dlist_head_t head);

/**
 * get the next entry refer to given entry and have no lock, the entry is not removed from list.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @param[in] entry		s5 dlist entry, given entry, if set to be NULL will get the first entry from list-head.
 * @return	non-NULL on success, NULL on failure.
 * @retval	non-NULL success.
 * @retval	NULL	  head or entry is NULL.
 * @retval	NULL	  head or entry is invalid.
 * @retval	NULL	  entry had been located in other list.
 */
ppf_dlist_entry_t s5list_next_ulc(ppf_dlist_head_t head, ppf_dlist_entry_t entry);

/**
 * get the next entry refer to given entry and have no lock, the entry is not removed from list.
 *
 * @param[in] head		s5 dlist head, pop from the head.
 * @param[in] entry		s5 dlist entry, given entry, if set to be NULL will get the first entry from list-tail.
 * @return	non-NULL on success, NULL on failure.
 * @retval	non-NULL success.
 * @retval	NULL	  head or entry is NULL.
 * @retval	NULL	  head or entry is invalid.
 * @retval	NULL	  entry had been located in other list.
 */
ppf_dlist_entry_t s5list_next_tail_ulc(ppf_dlist_head_t head, ppf_dlist_entry_t entry);


/**
 * get the count of list's entry.
 *
 * @param[in] head	s5 dlist head.
 * @return	count of list's entry.
 */

#define s5list_length(head)        (head)->count


/**
 * get the offset between structure of TYPE and it's MEMBER.
 *
 * @param[in] 	TYPE		structure's TYPE.
 * @param[in] 	MEMBER		structure's special field.
 * @return	the offset count of bytes.
 */
#define _offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/**
 * get the list entry.
 *
 * @param[in] ptr		structure's pointer.
 * @param[in] type		structure's type.
 * @param[in] member	structure's special field.
 * @return	list entry.
 */
#define S5LIST_ENTRY(ptr, type, member) (		\
		(type *)((char*)ptr - _offsetof(type, member)))


/**
 * enum all the entries of list by for loop.
 *
 * @param[in] pos	s5 dlist entry.
 * @param[in] count	temporary variable.
 * @param[in] head	s5 dlist head.
 * @return	list entry.
 */
#define S5LIST_FOR_EACH(pos, count, head) \
		for (pos = (head)->list, count = (head)->count; (count > 0) && pos; pos = pos->next, count--)

/**
 * enum all the entries of list by for loop.
 *
 * @param[in] pos	s5 dlist entry.
 * @param[in] count temporary variable.
 * @param[in] head	s5 dlist head.
 * @return	list entry.
 */
#define S5LIST_FOR_EACH_SAFE(pos, n, count, head) \
		for (pos = (head)->list, count = (head)->count, n = (pos ? pos->next : NULL); (count > 0) && (pos ? (n = pos->next, pos) : pos); pos = n, count--)
					

#ifdef __cplusplus
}
#endif


template <typename T>
class PfList
{
public:
	T* head;
	T* tail;
	T* T::* next;

	PfList(T* T::* n) :head(NULL), tail(NULL) {
		next = n;
	}

	void clear(){
		head=tail=NULL;
	}
	void inline push_front(T* e) {
		if (!head) { //an empty list
			head = e;
			tail = e;
			e->*next = NULL;
		}
		else {
			e->*next = head;
			head = e;
		}
	}

	void inline push_back(T* e) {
		if (!tail) { /*empty*/
			head = e;
		}
		else {
			tail->*next = e;
		}
		tail = e;
		e->*next = NULL;

	}
};


template<typename T>
class PfDoublyList {
public:
	T head;
	PfDoublyList() {
		head.list_next = &head;
		head.list_prev = &head;
	}
	inline __attribute__((always_inline)) void append(T* element) {
		element->list_next = head.list_next;
		element->list_prev = &head;
		head.list_next->list_prev = element;
		head.list_next = element;
	}

	inline __attribute__((always_inline)) void remove(T* element) {
		element->list_next->list_prev = element->list_prev;
		element->list_prev->list_next = element->list_next;
	}

	inline __attribute__((always_inline)) T* pop() {
		T* e = head.list_next;
		if (e == &head)
			return NULL;
		remove(e);
		return e;
	}
};


#endif


