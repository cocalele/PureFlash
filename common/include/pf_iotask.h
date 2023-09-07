#ifndef pf_iotask_h__
#define pf_iotask_h__

#include <libaio.h>
#include "pf_message.h"

class PfServerIocb;
class PfClientIocb;

struct SubTask
{
	PfOpCode opcode;
	//NOTE: any added member should be initialized either in PfDispatcher::init_mempools, or in PfServerIocb::setup_subtask
	union{
		PfServerIocb* parent_iocb;
		PfClientIocb* client_iocb;
	};
	uint64_t rep_id;
	uint64_t store_id;
	uint32_t task_mask;
	uint32_t rep_index; //task_mask = 1 << rep_index;
	PfMessageStatus complete_status;
	virtual void complete(PfMessageStatus comp_status);
	virtual void complete(PfMessageStatus comp_status, uint16_t meta_ver);
	//virtual PfEventQueue* half_complete(PfMessageStatus comp_status);

	SubTask() :opcode(PfOpCode(0)), parent_iocb(NULL), task_mask(0), rep_index(0), complete_status((PfMessageStatus)0) {}
};

struct IoSubTask : public SubTask
{
#pragma warning("Use union here is beter")
	struct iovec uring_iov;
	iocb aio_cb; //aio cb to perform io
	IoSubTask* next;//used for chain waiting io
	inline void complete_read_with_zero();

	IoSubTask() :next(NULL) {}
};
struct CowTask : public IoSubTask {
	off_t src_offset;
	off_t dst_offset;
	void* buf;
	int size;
	sem_t sem;
};
#endif // pf_iotask_h__