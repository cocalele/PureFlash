#ifndef pf_mempool_h__
#define pf_mempool_h__
#include <pthread.h>

#include "pf_fixed_size_queue.h"
#include "pf_lock.h"

template <class U>
class ObjectMemoryPool
{
public:
	pthread_spinlock_t lock;
	PfFixedSizeQueue<U*> free_obj_queue;
	U* data;
	int obj_count;

public:
	int init(int cap) {
		int rc = 0;
		obj_count = cap;
		rc = pthread_spin_init(&lock, 0);
		if (rc)
			return -rc;
		rc = free_obj_queue.init(cap);
		if(rc)
		{
			S5LOG_ERROR("Failed init queue in memory pool, rc:%d", rc);
			goto release1;
		}
		data = (U*)calloc(cap, sizeof(U));
		if(data == NULL)
		{
			rc = -ENOMEM;
			S5LOG_ERROR("Failed alloc memory pool, rc:%d, count:%d, size:%d", rc, sizeof(U), cap);
			goto release2;
		}
		for(int i=0;i<cap;i++)
		{
			free_obj_queue.enqueue(&data[i]);
		}
		return 0;
	release1:
		pthread_spin_destroy(&lock);
	release2:
		free_obj_queue.destroy();
		return rc;
	}

	U* alloc() {
		AutoSpinLock _l(&lock);
		if (free_obj_queue.is_empty())
			return NULL;
		return free_obj_queue.dequeue();
	}

	/**
	 * throw logic_error
	 */
	void free(U* p) {
		if (free_obj_queue.is_full())
			throw std::runtime_error("call free to full memory pool");
		free_obj_queue.enqueue(p);
	}
	void destroy() {
		if(data != NULL)
		{
			free_obj_queue.destroy();
			::free(data);
			pthread_spin_destroy(&lock);
			data = NULL;
		}
	}
	~ObjectMemoryPool()
	{
		destroy();
	}

	//remain object count
	int remain()
	{
		AutoSpinLock _l(&lock);
		return free_obj_queue.count();
	}
};
#endif // pf_mempool_h__
