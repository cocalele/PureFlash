#ifndef pf_mempool_h__
#define pf_mempool_h__
#include <pthread.h>
#include <malloc.h>

#include "pf_fixed_size_queue.h"
#include "pf_lock.h"
#include "spdk/env.h"

template <class U>
class ObjectMemoryPool
{
public:
	pthread_spinlock_t lock;
	PfFixedSizeQueue<U*> free_obj_queue;
	U* data;
	int obj_count;

public:
	ObjectMemoryPool() :data(NULL), obj_count(0) {}
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
			new (&data[i]) U(); //call constructor
			free_obj_queue.enqueue(&data[i]);
		}
		return 0;
	release2:
		free_obj_queue.destroy();
	release1:
		pthread_spin_destroy(&lock);
		return rc;
	}

	U* alloc() {
		pthread_spin_lock(&lock);
		if (free_obj_queue.is_empty()){
			pthread_spin_unlock(&lock);
			return NULL;
		}
		U* t = free_obj_queue.dequeue_nolock();
		pthread_spin_unlock(&lock);
		return t;
	}

	/**
	 * throw logic_error
	 */
	void free(U* p) {
		pthread_spin_lock(&lock);
		int rc = free_obj_queue.enqueue_nolock(p);
		pthread_spin_unlock(&lock);
		if (rc != 0) {
			S5LOG_ERROR("Failed to free obj:%p, pool is full", p);
			throw std::runtime_error("call free to full memory pool");
		}
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

class BigMemPool {
public:
	BigMemPool(size_t buf_size) { this->buf_size = buf_size; }
	void* alloc(size_t s, bool dma_buf) {
		if (dma_buf)
			return spdk_dma_zmalloc(s, 4096, NULL);
		else
			return memalign(4096, s);
	}
	void free(void* p, bool dma_buf) {
		if (dma_buf)
			spdk_dma_free(p);
		else
			::free(p);
	}
private:
	size_t buf_size;
};
#endif // pf_mempool_h__
