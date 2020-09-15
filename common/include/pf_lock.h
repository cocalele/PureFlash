#ifndef pf_lock_h__
#define pf_lock_h__
#include <pthread.h>

class AutoSpinLock {
	pthread_spinlock_t* lock;
public:
	inline AutoSpinLock(pthread_spinlock_t* l){
		this->lock = l;
		pthread_spin_lock(lock);
	}
	inline ~AutoSpinLock() {
		pthread_spin_unlock(lock);
	}

};

class AutoMutexLock {
	pthread_mutex_t* lock;
public:
	inline AutoMutexLock(pthread_mutex_t* l){
		this->lock = l;
		pthread_mutex_lock(lock);
	}
	inline ~AutoMutexLock() {
		pthread_mutex_unlock(lock);
	}

};
#endif // pf_lock_h__
