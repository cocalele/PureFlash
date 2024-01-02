/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef pf_replica_h__
#define pf_replica_h__

#include "pf_volume.h"
#include "pf_replicator.h"

class IoSubTask;
class PfFlashStore;


class PfLocalReplica : public PfReplica
{
public:
	virtual int submit_io(IoSubTask* subtask);
public:
	std::shared_ptr<PfFlashStore> disk;
};

class PfSyncRemoteReplica : public PfReplica
{
public:
	virtual int submit_io(IoSubTask* subtask);
public:
	std::shared_ptr<PfReplicator> replicator;
};

#endif // pf_replica_h__