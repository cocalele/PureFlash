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

class IoSubTask;
class S5FlashStore;
class BufferDescriptor;

enum HealthStatus : int32_t {
	HS_OK = 0,
	HS_ERROR = 1,
	HS_DEGRADED = 2
};
#define MAX_REP_COUNT 5 //3 for normal replica, 1 for remote replicating, 1 for recoverying
HealthStatus health_status_from_str(const std::string&  status_str);

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

	S5FlashStore * tray_inst;
public:
	virtual ~S5Replica() {} //to avoid compile warning
	virtual int submit_io(IoSubTask* subtask) = 0;
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

	~S5Shard();
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

	S5FixedSizeQueue<BufferDescriptor*> io_buffers;
	~S5Volume();
};

#endif // afs_volume_h__
