#ifndef pf_dispatcher_h__
#define pf_dispatcher_h__

#include "pf_volume.h"
#include <map>
#include <pf_event_thread.h>
#include <pf_connection.h>
#include "pf_mempool.h"

#define PF_MAX_SUBTASK_CNT 5 //1 local, 2 sync rep, 1 remote replicating, 1 rebalance
struct SubTask
{
	PfReplica* rep;
	uint32_t task_mask;
	uint32_t rep_index; //task_mask = 1 << rep_index;

};

struct IoSubTask : public SubTask
{
};

struct PfServerIocb
{
public:
	uint32_t task_mask;
	BufferDescriptor* cmd_bd;
	BufferDescriptor* data_bd;
	BufferDescriptor* reply_bd; //Used by dispatcher tasks

	PfConnection *conn;
	PfVolume* vol;
	BOOL is_timeout;

	SubTask* subtasks[PF_MAX_SUBTASK_CNT];
	IoSubTask io_subtasks[3];

	void inline setup_subtask();
};

class PfDispatcher : public PfEventThread
{
public:
	ObjectMemoryPool<PfServerIocb> iocb_pool;
	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
	std::map<uint64_t, PfVolume*> opened_volumes;

	//PfDispatcher(const std::string &name);
	int prepare_volume(PfVolume* vol);
	int dispatch_io(PfServerIocb *iocb);
	virtual int process_event(int event_type, int arg_i, void* arg_p);

	int init(int disp_idx);
	int init_mempools();
};
#endif // pf_dispatcher_h__
