#ifndef s5_lock_h__
#define s5_lock_h__

class AutoSpinLock {
	pthread_spin_lock* l;
public:
	inline AutoSpinLock(pthread_spin_lock& l){
		this->l = &l;
		pthread_spin_lock(l);
	}
	inline ~AutoSpinLock() {
		pthread_spin_unlock(l);
	}

};
#endif // s5_lock_h__
