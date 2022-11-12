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


class PfIoEngine
{
public:
	PfFlashStore* disk;
	int fd;

	PfIoEngine(PfFlashStore* d);
	virtual int init()=0;
	virtual int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len) = 0;
	virtual int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len) = 0;

};

class PfAioEngine : public PfIoEngine
{
public:
	io_context_t aio_ctx;

public:
	PfAioEngine(PfFlashStore* disk) :PfIoEngine(disk) {};
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);

	std::thread aio_poller;
	void polling_proc();
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
};

#endif //PUREFLASH_PF_IOENGINE_H
