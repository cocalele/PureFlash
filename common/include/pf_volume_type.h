#ifndef pf_volume_type_h__
#define pf_volume_type_h__


#define SHARD_ID(x)        ((x) & 0xffffffffffffff00LL)
#define SHARD_INDEX(x)    (((x) & 0x0000000000ffff00LL) >> 8)
#define REPLICA_INDEX(x)   ((x) & 0x00000000000000ffLL)
#define VOLUME_ID(x)        ((x) & 0xffffffffff000000LL)

struct volume_id_t {
	uint64_t vol_id;

	inline __attribute__((always_inline)) volume_id_t(uint64_t id) : vol_id(id) {}
	inline __attribute__((always_inline)) uint64_t val() const { return vol_id; }
};
#define int64_to_volume_id(x) ((volume_id_t) { x })

struct shard_id_t {
	uint64_t shard_id;

	inline __attribute__((always_inline)) shard_id_t(uint64_t id) : shard_id(id) {}
	inline __attribute__((always_inline)) uint64_t val() const { return shard_id; }
	inline __attribute__((always_inline)) volume_id_t to_volume_id() const { return  (volume_id_t) { VOLUME_ID(shard_id) }; }
	inline __attribute__((always_inline)) uint32_t shard_index() const { return SHARD_INDEX(shard_id); }
};
#define int64_to_shard_id(x) ((shard_id_t) { x })

struct replica_id_t {
	uint64_t rep_id;

	inline __attribute__((always_inline)) uint64_t val() const { return rep_id; }
	inline __attribute__((always_inline)) shard_id_t to_shard_id() const { return (shard_id_t) { SHARD_ID(rep_id) }; }
	inline __attribute__((always_inline)) volume_id_t to_volume_id() const { return (volume_id_t) { VOLUME_ID(rep_id) }; }
	inline __attribute__((always_inline)) uint32_t shard_index() const { return SHARD_INDEX(rep_id); }
	inline __attribute__((always_inline)) uint32_t replica_index() const { return REPLICA_INDEX(rep_id); }

	inline __attribute__((always_inline)) explicit  replica_id_t(uint64_t id) : rep_id(id) {}
};
#define int64_to_replica_id(x) ((replica_id_t) { (uint64_t)(x) })


enum HealthStatus : int32_t {
	HS_OK = 0,
	HS_ERROR = 1,
	HS_DEGRADED = 2,
	HS_RECOVERYING = 3,
};
#define MAX_REP_COUNT 5 //3 for normal replica, 1 for remote replicating, 1 for recoverying
HealthStatus health_status_from_str(const std::string& status_str);
const char* HealthStatus2Str(HealthStatus code);

#define CUT_LOW_10BIT(x) (((unsigned long)(x)) & 0xfffffffffffffc00L)
#define vol_offset_to_block_slba(offset, obj_size_order) (((offset) >> (obj_size_order)) << (obj_size_order - LBA_LENGTH_ORDER))
#define offset_in_block(offset, in_obj_offset_mask) ((offset) & (in_obj_offset_mask))

#define META_RESERVE_SIZE (40LL<<30) //40GB, can be config in conf
#define MIN_META_RESERVE_SIZE (10LL<<30) //10GB, can be config in conf

#define MD5_RESULT_LEN (16)

enum {
	FIRST_METADATA_ZONE,
	SECOND_METADATA_ZONE,
	FIRST_REDOLOG_ZONE,
	SECOND_REDOLOG_ZONE,
};

enum {
	FREELIST,
	TRIMLIST,
	LMT,
	METADATA,
	REDOLOG,
	CURRENT,
	OPPOSITE,
};
#define S5_VERSION_V2 0x00020000
#define S5_VERSION 0x00030000


#define OFFSET_HEAD 0
#define OFFSET_FREE_LIST_FIRST (4096)
#define OFFSET_TRIM_LIST_FIRST (128LL<<20)
#define OFFSET_LMT_MAP_FIRST (256LL<<20)

#define OFFSET_FREE_LIST_SECOND (3LL << 30)
#define OFFSET_TRIM_LIST_SECOND (OFFSET_FREE_LIST_SECOND + (128LL<<20))
#define OFFSET_LMT_MAP_SECOND (OFFSET_FREE_LIST_SECOND + (256LL<<20))


#define OFFSET_REDO_LOG_FIRST (6LL<<30)
#define REDO_LOG_SIZE (256LL<<20) //256M
#define OFFSET_REDO_LOG_SECOND (OFFSET_REDO_LOG_FIRST + REDO_LOG_SIZE)
static_assert(OFFSET_REDO_LOG_SECOND + REDO_LOG_SIZE < MIN_META_RESERVE_SIZE, "OFFSET_REDO_LOG exceed reserve area");
#define OFFSET_GLOBAL_META_LOCK ((2<<30)+(512<<20)) //2G + 512M

#define COW_OBJ_SIZE (128LL<<10)
#define RECOVERY_IO_SIZE (128<<10) //recovery read IO size
#define DEFAULT_OBJ_SIZE (64<<20)
#define DEFAULT_OBJ_SIZE_ORDER 26 // DEFAULT_OBJ_SIZE=1<<DEFAULT_OBJ_SIZE_ORDER


#endif // pf_volume_type_h__
