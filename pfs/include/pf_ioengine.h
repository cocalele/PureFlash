/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */


#ifndef PUREFLASH_PF_IOENGINE_H
#define PUREFLASH_PF_IOENGINE_H

#include <stdint.h>
#include <libaio.h>
#include <thread>

#include "liburing.h"



class PfFlashStore;
class IoSubTask;

#define MAX_AIO_DEPTH 4096

struct ns_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
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

class PfIoEngine
{
public:
	PfFlashStore* disk;
	union {
		int fd;
		struct ns_entry *ns;
	};

	PfIoEngine(PfFlashStore* d);
	virtual int init()=0;
	virtual int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len) = 0;
	virtual int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len) = 0;
	virtual int submit_batch(){return 0;};
	//virtual int poll_io(int *completions) = 0;
   	virtual uint64_t sync_read(void *buffer, uint64_t buf_size, uint64_t offset) = 0;
    virtual uint64_t sync_write(void *buffer, uint64_t buf_size, uint64_t offset) = 0;
	virtual uint64_t get_device_cap() = 0;
	virtual ~PfIoEngine(){}
};

#define BATCH_IO_CNT  512
class PfAioEngine : public PfIoEngine
{
public:
	io_context_t aio_ctx;
	struct iocb* batch_iocb[BATCH_IO_CNT];
	int batch_io_cnt=0;
public:
	PfAioEngine(PfFlashStore* disk) :PfIoEngine(disk) {};
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);
	int submit_batch();
	std::thread aio_poller;
	void polling_proc();

    uint64_t sync_read(void *buffer, uint64_t buf_size, uint64_t offset);
    uint64_t sync_write(void *buffer, uint64_t buf_size, uint64_t offset);
	uint64_t get_device_cap();
	//int poll_io(int *completions);
};


class PfIouringEngine : public PfIoEngine
{
	struct io_uring uring;
	int seg_cnt_per_dispatcher;
public:
	PfIouringEngine(PfFlashStore* disk) :PfIoEngine(disk) {};
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);
	std::thread iouring_poller;
	void polling_proc();

    uint64_t sync_read(void *buffer, uint64_t buf_size, uint64_t offset);
    uint64_t sync_write(void *buffer, uint64_t buf_size, uint64_t offset);
	uint64_t get_device_cap();
	//int poll_io(int *completions);
};


/*
 *  first qpair is for sync IO
 *  
 */
#define QPAIRS_CNT 2

class PfspdkEngine : public PfIoEngine
{
	int num_qpairs;
	struct spdk_nvme_qpair **qpair;
	struct spdk_nvme_poll_group	*group;
public:
	PfspdkEngine(PfFlashStore* disk) :PfIoEngine(disk) {};
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);	
	static int poll_io(int *completions, void *arg);
	static void spdk_io_complete(void *ctx, const struct spdk_nvme_cpl *cpl);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);
	static void spdk_cow_io_complete(void *ctx, const struct spdk_nvme_cpl *cpl);
	int submit_scc(uint64_t media_len, off_t src, off_t dest, void* (*scc_cb)(void *ctx), void *arg);
	static void scc_complete(void *arg, const struct spdk_nvme_cpl *cpl);

	uint64_t sync_write(void *buffer, uint64_t buf_size, uint64_t offset);
	uint64_t sync_read(void *buffer, uint64_t buf_size, uint64_t offset);
	uint64_t get_device_cap();

	void spdk_nvme_disconnected_qpair_cb(struct spdk_nvme_qpair *qpair, void *poll_group_ctx);
	uint64_t spdk_nvme_bytes_to_blocks(uint64_t offset_bytes, uint64_t *offset_blocks,
		uint64_t num_bytes, uint64_t *num_blocks);

};

#endif //PUREFLASH_PF_IOENGINE_H
