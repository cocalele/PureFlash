/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef afs_volume_h__
#define afs_volume_h__

#include <string.h>
#include <list>
#include <stdint.h>
#include <vector>
#include <string>
#include "basetype.h"
#include "pf_fixed_size_queue.h"
#include "pf_volume_type.h"

class IoSubTask;
class PfFlashStore;
class BufferDescriptor;

//Replica represent a replica of shard
class PfReplica
{
public:
	enum HealthStatus status;
	uint64_t id;
	uint64_t store_id;
	bool is_local;
	bool is_primary;
	int	rep_index;
	int	ssd_index;
public:
	virtual ~PfReplica() {} //to avoid compile warning
	virtual int submit_io(IoSubTask* subtask) = 0; //override in pf_replica.h
};


//Shard represent a shard of volume
struct PfShard
{
	uint64_t id;
	int	shard_index;
	struct PfReplica*	replicas[MAX_REP_COUNT];
	int primary_replica_index;
	int duty_rep_index; //which replica the current store node is responsible for
	BOOL is_primary_node; //is current node the primary node of this shard
	int rep_count;
	int snap_seq;
	enum HealthStatus status;
	uint64_t meta_ver;

	~PfShard();
};
//Volume represent a Volume,
struct PfVolume
{
	char name[128];
	uint64_t id;
	uint64_t size;
	int	rep_count;
	int shard_count;
	std::vector<PfShard*>	shards;
	int snap_seq;
	enum HealthStatus status;
	uint64_t meta_ver;

//	PfFixedSizeQueue<BufferDescriptor*> io_buffers;

	PfVolume() : _ref_count(1) {/*other member will inited in convert_argument_to_volume*/}
	inline void add_ref() { __sync_fetch_and_add(&_ref_count, 1); }
	inline void dec_ref() {
		__sync_fetch_and_sub(&_ref_count, 1);
		if(_ref_count == 0)
			delete this;
	}
	PfVolume& operator=(PfVolume&& nv);
private:
	~PfVolume();
	int _ref_count; //name similar with rep_count, so add prefix with _
};

#endif // afs_volume_h__
