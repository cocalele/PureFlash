#ifndef s5_replica_h__
#define s5_replica_h__

#include "afs_volume.h"

class IoSubTask;
class S5FlashStore;


class S5LocalReplica : public S5Replica
{
public:
	virtual int submit_io(IoSubTask* subtask);
public:
	S5FlashStore* store;
};
#endif // s5_replica_h__