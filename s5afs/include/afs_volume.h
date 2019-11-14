#ifndef afs_volume_h__
#define afs_volume_h__

#include <string.h>
#include <list>

#define SHARD_ID(x)        ((x) & 0xffffffffffffff00LL)
#define SHARD_INDEX(x)    (((x) & 0x0000000000ffff00LL) >> 8)
#define REPLICA_INDEX(x)   ((x) & 0x00000000000000ffLL)

//Replica represent a replica of shard
class S5Replica
{
public:
	enum HealthStatus status;
	uint64_t id;
	uint64_t store_id;
	bool is_local;
	bool is_primary;
	int	rep_index;
	int	ssd_index;
};

//Shard represent a shard of volume
struct S5Shard
{
	uint64_t id;
	int	shard_index;
	struct S5Replica*	replicas[MAX_REP_COUNT];
	int primary_replica_index;
	int duty_rep_index; //which replica the current store node is responsible for
	BOOL is_primary_node; //is current node the primary node of this shard
	int rep_count;
	int snap_seq;
	enum HealthStatus status;
	uint64_t meta_ver;
};
//Volume represent a Volume,
struct S5Volume
{
	char name[128];
	uint64_t id;
	uint64_t size;
	int	rep_count;
	int shard_count;
	std::vector<S5Shard*>	shards;
	int snap_seq;
	enum HealthStatus status;
	uint64_t meta_ver;
};

#endif // afs_volume_h__
