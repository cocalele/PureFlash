#ifndef pf_iouring_engine_h__
#define pf_iouring_engine_h__

#include <stdint.h>
#include <libaio.h>
#include <thread>
#include <string>

#include <liburing.h>
#include "pf_ioengine.h"

class PfIouringEngine : public PfIoEngine
{
	int fd;
	struct io_uring uring;
	int seg_cnt_per_dispatcher;
public:
	PfIouringEngine(const char* name, int _fd) :PfIoEngine(name), fd(_fd) {};
	int init();
	int submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len);
	int submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len);
	std::thread iouring_poller;
	void polling_proc();

	uint64_t sync_read(void* buffer, uint64_t buf_size, uint64_t offset);
	uint64_t sync_write(void* buffer, uint64_t buf_size, uint64_t offset);
	uint64_t get_device_cap();
	//int poll_io(int *completions);
};
#endif // pf_iouring_engine_h__