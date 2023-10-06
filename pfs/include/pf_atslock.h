#ifndef pf_atslock_h__
#define pf_atslock_h__

int pf_ats_lock(int devfd, int64_t lock_location);

int pf_ats_unlock(int devfd, int64_t lock_location);
#endif // pf_atslock_h__
