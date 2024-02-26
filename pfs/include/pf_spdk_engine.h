#ifndef pf_spdk_engine_h__
#define pf_spdk_engine_h__

#include "pf_ioengine.h"
struct ns_entry {
	struct spdk_nvme_ctrlr* ctrlr;
	struct spdk_nvme_ns* ns;
	uint16_t nsid;
	/** Size in bytes of a logical block*/
	uint32_t block_size;
	/** Number of blocks */
	uint64_t block_cnt;
	/** Size in bytes of a metadata for the backend */
	uint32_t md_size;
	/**
	 * Specify metadata location and set to true if metadata is interleaved
	 * with block data or false if metadata is separated with block data.
	 *
	 * Note that this field is valid only if there is metadata.
	 */
	bool md_interleave;

	bool scc;
};


struct pf_io_channel {
	int num_qpairs;
	struct spdk_nvme_qpair** qpair;
	struct spdk_nvme_poll_group* group;
};


/*
 *  first qpair is for sync IO
 *
 */
#define QPAIRS_CNT 2

class PfspdkEngine : public PfIoEngine
{
	struct ns_entry* ns;
public:
	PfspdkEngine(PfFlashStore* disk, struct ns_entry* _ns) :PfIoEngine(disk->tray_name), ns(_ns) {};
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);
	static int poll_io(int* completions, void* arg);
	static void spdk_io_complete(void* ctx, const struct spdk_nvme_cpl* cpl);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);
	static void spdk_cow_io_complete(void* ctx, const struct spdk_nvme_cpl* cpl);
	int submit_scc(uint64_t media_len, off_t src, off_t dest, void* (*scc_cb)(void* ctx), void* arg);
	static void scc_complete(void* arg, const struct spdk_nvme_cpl* cpl);

	uint64_t sync_write(void* buffer, uint64_t buf_size, uint64_t offset);
	uint64_t sync_read(void* buffer, uint64_t buf_size, uint64_t offset);
	uint64_t get_device_cap();

	void spdk_nvme_disconnected_qpair_cb(struct spdk_nvme_qpair* qpair, void* poll_group_ctx);
	uint64_t spdk_nvme_bytes_to_blocks(uint64_t offset_bytes, uint64_t* offset_blocks,
		uint64_t num_bytes, uint64_t* num_blocks);
	int pf_spdk_io_channel_open(int num_qpairs);
	int pf_spdk_io_channel_close(struct pf_io_channel *pic);
};

#endif // pf_spdk_engine_h__