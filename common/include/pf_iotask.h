#ifndef pf_iotask_h__
#define pf_iotask_h__

#include <libaio.h>
#include <semaphore.h>

#include "pf_message.h"
#include "pf_iotask.h"
#include "pf_buffer.h"

class PfServerIocb;
class PfClientIocb;
struct BufferDescriptor;
struct SubTask;
struct IoSubTask;
#define PF_MAX_SUBTASK_CNT 5 //1 local, 2 sync rep, 1 remote replicating, 1 rebalance
struct PfIocb {
public:
	BufferDescriptor* cmd_bd;
	BufferDescriptor* data_bd;
	BufferDescriptor* reply_bd; 
	uint32_t task_mask;



	SubTask* subtasks[PF_MAX_SUBTASK_CNT];
};

struct TaskCompleteOps
{

	void (*complete)(SubTask* t, PfMessageStatus comp_status);
	void (*complete_meta_ver)(SubTask* t, PfMessageStatus comp_status, uint16_t meta_ver);

};

struct SubTask  
{
	PfOpCode opcode;
	//NOTE: any added member should be initialized either in PfDispatcher::init_mempools, or in PfServerIocb::setup_subtask
	PfIocb* parent_iocb;
	uint64_t rep_id;
	uint64_t store_id;
	uint32_t task_mask;
	uint32_t rep_index; //task_mask = 1 << rep_index;
	uint64_t submit_time;
	uint64_t reply_time;
	PfMessageStatus complete_status;
	TaskCompleteOps *ops = NULL;
	//virtual PfEventQueue* half_complete(PfMessageStatus comp_status);

	SubTask() :opcode(PfOpCode(0)), parent_iocb(NULL), task_mask(0), rep_index(0),
	                  submit_time(0), reply_time(0), complete_status((PfMessageStatus)0) {}
};

struct IoSubTask : public SubTask
{
	struct iovec uring_iov;
	iocb aio_cb; //aio cb to perform io, union with uring_iov

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
inline void IoSubTask::complete_read_with_zero() {
	//    PfMessageHead* cmd = parent_iocb->cmd_bd->cmd_bd;
	BufferDescriptor* data_bd = parent_iocb->data_bd;

	memset(data_bd->buf, 0, data_bd->data_len);
	ops->complete(this, PfMessageStatus::MSG_STATUS_SUCCESS);
}

#endif // pf_iotask_h__