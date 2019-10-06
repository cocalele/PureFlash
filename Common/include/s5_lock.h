#ifndef s5_lock_h__
#define s5_lock_h__
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
#endif // s5_lock_h__
