#ifndef afs_volume_h__
#define afs_volume_h__

#include <string.h>
#include <list>
#include <stdint.h>
#include <vector>
#include <string>
#include "basetype.h"
#include "pf_fixed_size_queue.h"



#define SHARD_ID(x)        ((x) & 0xffffffffffffff00LL)
#define SHARD_INDEX(x)    (((x) & 0x0000000000ffff00LL) >> 8)
#define REPLICA_INDEX(x)   ((x) & 0x00000000000000ffLL)
#define VOLUME_ID(x)        ((x) & 0xffffffffff000000LL)
class IoSubTask;
class PfFlashStore;
class BufferDescriptor;
struct volume_id_t{
	uint64_t vol_id;

	inline __attribute__((always_inline)) volume_id_t(uint64_t id) : vol_id(id){}
	inline __attribute__((always_inline)) uint64_t val() const { return vol_id; }
};
#define int64_to_volume_id(x) ((volume_id_t) { x })

struct shard_id_t {
	uint64_t shard_id;

	inline __attribute__((always_inline)) shard_id_t(uint64_t id) : shard_id(id){}
	inline __attribute__((always_inline)) uint64_t val() { return shard_id; }
	inline __attribute__((always_inline)) volume_id_t to_volume_id() const { return  (volume_id_t) { VOLUME_ID(shard_id)}; }
	inline __attribute__((always_inline)) uint32_t shard_index() const { return SHARD_INDEX(shard_id); }
};
#define int64_to_shard_id(x) ((shard_id_t) { x })

struct replica_id_t {
	uint64_t rep_id;

	inline __attribute__((always_inline)) uint64_t val() { return rep_id; }
	inline __attribute__((always_inline)) shard_id_t to_shard_id() const { return (shard_id_t){SHARD_ID(rep_id)}; }
	inline __attribute__((always_inline)) volume_id_t to_volume_id() const { return (volume_id_t) { VOLUME_ID(rep_id)}; }
	inline __attribute__((always_inline)) uint32_t shard_index() const { return SHARD_INDEX(rep_id); }
	inline __attribute__((always_inline)) uint32_t replica_index() const { return REPLICA_INDEX(rep_id); }

	inline __attribute__((always_inline))  replica_id_t(uint64_t id) : rep_id(id){}
};
#define int64_to_replica_id(x) ((replica_id_t) { (uint64_t)(x) })


enum HealthStatus : int32_t {
	HS_OK = 0,
	HS_ERROR = 1,
	HS_DEGRADED = 2,
	HS_RECOVERYING  = 3,
};
#define MAX_REP_COUNT 5 //3 for normal replica, 1 for remote replicating, 1 for recoverying
HealthStatus health_status_from_str(const std::string&  status_str);

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

	PfFixedSizeQueue<BufferDescriptor*> io_buffers;

	PfVolume() : _ref_count(1) {/*other member will inited in convert_argument_to_volume*/}
	inline void add_ref() { __sync_fetch_and_add(&_ref_count, 1); }
	inline void dec_ref() {
		__sync_fetch_and_sub(&_ref_count, 1);
		if(_ref_count == 0)
			delete this;
	}
private:
	~PfVolume();
	int _ref_count; //name similar with rep_count, so add prefix with _
};

#endif // afs_volume_h__
