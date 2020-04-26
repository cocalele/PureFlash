#ifndef pf_replica_h__
#define pf_replica_h__

#include "pf_volume.h"

class IoSubTask;
class S5FlashStore;


class S5LocalReplica : public S5Replica
{
public:
	virtual int submit_io(IoSubTask* subtask);
public:
	S5FlashStore* store;
};
#endif // pf_replica_h__