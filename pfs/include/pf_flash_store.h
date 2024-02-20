/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef flash_store_h__
#define flash_store_h__
#include <uuid/uuid.h>
#include <unordered_map>
#include <stdint.h>
#include <libaio.h>
#include <thread>

#include "pf_fixed_size_queue.h"
#include "basetype.h"
#include "pf_conf.h"
#include "pf_mempool.h"
#include "pf_event_queue.h"
#include "pf_event_thread.h"
#include "pf_tray.h"
#include "pf_threadpool.h"
#include "pf_volume.h"
#include "pf_bitmap.h"
#include "pf_ioengine.h"
#include "pf_lmt.h"


class PfRedoLog;


struct scc_cow_context
{
	lmt_key *key;
	lmt_entry *srcEntry;
	lmt_entry *dstEntry;
	PfFlashStore *pfs;
	int rc;
};


struct RecoveryContext;

class PfFlashStore : public PfEventThread
{
public:
	struct HeadPage {
		uint32_t magic;
		uint32_t version;
		unsigned char uuid[16];
		uint32_t key_size;
		uint32_t entry_size;
		uint64_t objsize;
		uint64_t tray_capacity;
		uint64_t meta_size;
		uint32_t objsize_order; //objsize = 2 ^ objsize_order
		uint32_t rsv1; //to make alignment at 8 byte
		uint64_t free_list_position_first;
		uint64_t free_list_position_second;
		uint64_t free_list_size_first;
		uint64_t free_list_size_second;
		uint64_t trim_list_position_first;
		uint64_t trim_list_position_second;
		uint64_t trim_list_size;
		uint64_t lmt_position_first;
		uint64_t lmt_position_second;
		uint64_t lmt_size;
		uint64_t redolog_position_first;
		uint64_t redolog_position_second;
		uint64_t redolog_size;
		/**update after save metadata**/
		int64_t  redolog_phase;
		uint8_t  current_metadata;
		uint8_t  current_redolog;
		char md5_first[MD5_RESULT_LEN];
		char md5_second[MD5_RESULT_LEN];
		/***/
		char create_time[32];
	};

	//following are hot variables used by every IO. Put compact for cache hit convenience
	union {
		int fd;
		struct ns_entry *ns;
	};
	uint64_t in_obj_offset_mask; // := obj_size -1,

	std::unordered_map<struct lmt_key, struct lmt_entry*, struct lmt_hash> obj_lmt; //act as lmt table in S5
	PfFixedSizeQueue<int32_t> free_obj_queue;
	PfFixedSizeQueue<int32_t> trim_obj_queue;
	ObjectMemoryPool<lmt_entry> lmt_entry_pool;
	PfRedoLog* redolog;
	PfIoEngine* ioengine;
	HeadPage head;
	char tray_name[128];
	char uuid_str[64];

	pthread_mutex_t md_lock;
	pthread_cond_t md_cond;
#define COMPACT_IDLE 0
#define COMPACT_TODO 1
#define COMPACT_STOP 2
#define COMPACT_RUNNING 3
#define COMPACT_ERROR 4
	std::atomic<int> to_run_compact;
	PfFlashStore *compact_tool;
	int compact_lmt_exist;
	int is_shared_disk;
	PfFlashStore(int32_t n_threads = 4) : cow_thread_pool(n_threads) {}
	~PfFlashStore();
	/**
	 * init flash store from device. this function will create meta data
	 * and initialize the device if a device is not initialized.
	 *
	 * @return 0 on success, negative for error
	 * @retval -ENOENT  device not exist or failed to open
	 */
	int init(const char* dev_name, uint16_t* p_id);
	int shared_disk_init(const char* tray_name, uint16_t* p_id);
	int owner_init();
	int spdk_nvme_init(const char* trid_str, uint16_t* p_id);
	int register_controller(const char *trid_str);

	int process_event(int event_type, int arg_i, void* arg_p, void* arg_q);


	void trim_proc();
	std::thread trimming_thread;

	/**
	 * read data to buffer.
	 * a LBA is a block of data 4096 bytes.
	 * @return actual data length that readed to buf, in lba.
	 *         negative value for error
	 * actual data length may less than nlba in request to read. in this case, caller
	 * should treat the remaining part of buffer as 0.
	 */
	inline int do_read(IoSubTask* io);

	/**
	 * write data to flash store.
	 * a LBA is a block of data 4096 bytes.
	 * @return number of lbas has written to store
	 *         negative value for error
	 */
	int do_write(IoSubTask* io);

	int save_meta_data(int md_zone);
	int compact_tool_init();
	int compact_meta_data();
	int meta_data_compaction_trigger(int state, bool force_wait);
	uint64_t get_meta_position(int meta_type, int which);
	const char* meta_positon_2str(int meta_type, int which);
	int oppsite_md_zone();
	int oppsite_redolog_zone();
	void delete_snapshot(shard_id_t shard_id, uint32_t snap_seq_to_del, uint32_t prev_snap_seq, uint32_t next_snap_seq);
	int recovery_replica(replica_id_t  rep_id, const std::string &from_store_ip, int32_t from_store_id,
					  const std::string& from_ssd_uuid, int64_t object_size, uint16_t meta_ver);
	int delete_replica(replica_id_t rep_id);

	int get_snap_list(volume_id_t volume_id, int64_t offset, std::vector<int>& snap_list);
	int delete_obj(struct lmt_key* , struct lmt_entry* entry);
	int delete_obj_by_snap_seq(struct lmt_key* key, uint32_t snap_seq);
	virtual int commit_batch() override { return ioengine->submit_batch(); };
private:
	ThreadPool cow_thread_pool; //TODO: use std::async replace
	int format_disk();
	int read_store_head();
	int write_store_head();
	int initialize_store_head();
	int load_meta_data(int md_zone, bool compaction);
	int start_metadata_service(bool init);

	/** these two function convert physical object id (i.e. object in disk space) and offset in disk
	 *  Note: don't confuse these function with vol_offset_to_block_idx, which do convert
	 *        in volume space
	 */
	inline int64_t obj_id_to_offset(int64_t obj_id) { return (obj_id << head.objsize_order) + head.meta_size; }
	inline int64_t offset_to_obj_id(int64_t offset) { return (offset - head.meta_size) >> head.objsize_order; }
	void begin_cow(lmt_key* key, lmt_entry *objEntry, lmt_entry *dstEntry);
	void do_cow_entry(lmt_key* key, lmt_entry *objEntry, lmt_entry *dstEntry);
	void begin_cow_scc(lmt_key* key, lmt_entry *objEntry, lmt_entry *dstEntry);
	static void* end_cow_scc(void *ctx);
	int delete_obj_snapshot(uint64_t volume_id, int64_t slba, uint32_t snap_seq, uint32_t prev_snap_seq, uint32_t next_snap_seq);
	int recovery_write(lmt_key* key, lmt_entry * head, uint32_t snap_seq, void* buf, size_t length, off_t offset);
	int finish_recovery_object(lmt_key* key, lmt_entry * head, size_t length, off_t offset, int failed);
	int recovery_object_series(struct RecoveryContext& recov_ctx, lmt_key& key, int64_t offset);
	int recovery_single_object_entry(struct RecoveryContext& recov_ctx, lmt_key& key, uint32_t target_snap_seq, int64_t offset);


	void post_load_fix();
	void post_load_check();

	friend class PfRedoLog;
};

void delete_matched_entry(struct lmt_entry **head_ref, std::function<bool(struct lmt_entry *)> match,
                          std::function<void(struct lmt_entry *)> free_func);
#endif // flash_store_h__
