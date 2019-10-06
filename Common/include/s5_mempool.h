#ifndef s5_mempool_h__
#define s5_mempool_h__
#include <pthread.h>

#include "s5_fixed_size_queue.h"
#include "s5_lock.h"

template<typename T>
class ObjectMemoryPool
{
public:
	pthread_spinlock_t lock;
	S5FixedSizeQueue<T*> free_obj_queue;
	T* data;
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
		data = (T*)calloc(cap, sizeof(T));
		if(data == NULL)
		{
			rc = -ENOMEM;
			S5LOG_ERROR("Failed alloc memory pool, rc:%d, count:%d, size:%d", rc, sizeof(T), cap);
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

	T* alloc() {
		AutoSpinLock _l(&lock);
		if (free_obj_queue.is_empty())
			return NULL;
		return free_obj_queue.dequeue();
	}

	/**
	 * throw logic_error
	 */
	void free(T* p) {
		if (free_obj_queue.is_full())
			throw std::logic_error("call free to full memory pool");
		free_obj_queue.enqueue(p);
	}
	void destroy() {
		if(data != NULL)
		{
			free_obj_queue.destroy();
			free(data);
			pthread_spin_destroy(&lock);
			data = NULL;
		}
	}
	~ObjectMemoryPool()
	{
		destroy();
	}
};
#endif // s5_mempool_h__
