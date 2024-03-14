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
#include <string>

#include "basetype.h"

class PfFlashStore;
class IoSubTask;
class PfAppCtx;

#define MAX_AIO_DEPTH 4096

struct ns_entry;
uint64_t fd_get_cap(int fd);
class PfIoEngine
{
public:
	std::string disk_name;
	PfIoEngine(const char* name):disk_name(name){};
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
	int fd;
	io_context_t aio_ctx;
	struct iocb* batch_iocb[BATCH_IO_CNT];
	int batch_io_cnt=0;
	PfAppCtx *app_ctx;
public:
	PfAioEngine(const char* name, int _fd, PfAppCtx* ctx) :PfIoEngine(name), fd(_fd), app_ctx(ctx) {};
	~PfAioEngine();
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);
	virtual int submit_batch();
	std::thread aio_poller;
	void polling_proc();

    uint64_t sync_read(void *buffer, uint64_t buf_size, uint64_t offset);
    uint64_t sync_write(void *buffer, uint64_t buf_size, uint64_t offset);
	uint64_t get_device_cap();
	//int poll_io(int *completions);
};


BOOL is_disk_clean(PfIoEngine* eng);

#endif //PUREFLASH_PF_IOENGINE_H
