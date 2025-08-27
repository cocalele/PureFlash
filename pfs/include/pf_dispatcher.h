/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef pf_dispatcher_h__
#define pf_dispatcher_h__

#include <unordered_map>
#include <libaio.h>
#include "pf_event_thread.h"
#include "pf_connection.h"
#include "pf_mempool.h"
#include "pf_volume.h"
#include "pf_message.h"
#include "pf_stat.h" //for class DispatchStat
#ifdef WITH_RDMA
#include <rdma/rdma_cma.h>
#include "pf_rdma_connection.h"
#endif
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include "pf_iotask.h"

struct PfServerIocb;
class PfFlashStore;
struct lmt_entry;
struct lmt_key;


extern TaskCompleteOps _recovery_complete_ops;
struct RecoverySubTask : public IoSubTask
{
	BufferDescriptor *recovery_bd;
	uint64_t volume_id;
	int64_t offset;
	int64_t length;
	uint32_t  snap_seq;
	uint16_t meta_ver;
	sem_t* sem;
	ObjectMemoryPool<RecoverySubTask>* owner_queue;

	RecoverySubTask() : recovery_bd(NULL), volume_id(0), offset(0), length(0), snap_seq(0), sem(NULL){ ops = &_recovery_complete_ops;}
};


struct PfServerIocb : public PfIocb
{
public:
	PfConnection *conn;
	//PfVolume* vol;
	uint64_t vol_id;
	PfMessageStatus complete_status;
	uint16_t  complete_meta_ver;
	uint32_t ref_count;
	int disp_index;
	BOOL is_timeout;
	uint64_t received_time;
	uint64_t received_time_hz;
	uint64_t submit_rep_time;
	int primary_rep_index;
	uint64_t remote_rep1_cost_time;
	uint64_t remote_rep1_submit_cost;
	uint64_t remote_rep2_cost_time;
	uint64_t remote_rep2_submit_cost;
	uint64_t remote_rep1_reply_cost;
	uint64_t remote_rep2_reply_cost;
	uint64_t local_cost_time;
	IoSubTask io_subtasks[3];

	void inline setup_subtask(PfShard* s, PfOpCode opcode)
	{
		for (int i = 0; i < s->rep_count; i++) {
			if (s->replicas[i] == NULL) {
				continue;
			}
			if (s->replicas[i]->status == HS_OK || s->replicas[i]->status == HS_RECOVERYING) {
				subtasks[i]->complete_status = PfMessageStatus::MSG_STATUS_SUCCESS;
				subtasks[i]->opcode = opcode;  //subtask opcode will be OP_WRITE or OP_REPLICATE_WRITE
				task_mask |= subtasks[i]->task_mask;
				add_ref();
			}
		}
	}
	
	void inline setup_one_subtask(PfShard* s, int rep_index, PfOpCode opcode)
	{
		subtasks[rep_index]->complete_status=PfMessageStatus::MSG_STATUS_SUCCESS;
		subtasks[rep_index]->opcode = opcode;
		task_mask |= subtasks[rep_index]->task_mask;
		add_ref();
	}

	inline void add_ref() { __sync_fetch_and_add(&ref_count, 1); }
    inline void dec_ref();
	void dec_ref_on_error();
	inline void re_init();
private:
	void free_to_pool();
};

class PfDispatcher : public PfEventThread
{
public:
	ObjectMemoryPool<PfServerIocb> iocb_pool;
	struct disp_mem_pool mem_pool;
	std::unordered_map<uint64_t, PfVolume*> opened_volumes;
	int disp_index;

	DispatchStat stat;

	//PfDispatcher(const std::string &name);
	int prepare_volume(PfVolume* vol);
	int delete_volume(uint64_t vol_id);
	inline int dispatch_io(PfServerIocb *iocb);
	int dispatch_complete(SubTask*);
	virtual int process_event(int event_type, int arg_i, void* arg_p, void* arg_q);

	int init(int disp_idx, uint16_t* p_id);
	int init_mempools(int disp_idx);

	int dispatch_write(PfServerIocb* iocb, PfVolume* vol, PfShard * s);
	int dispatch_read(PfServerIocb* iocb, PfVolume* vol, PfShard * s);
	int dispatch_rep_write(PfServerIocb* iocb, PfVolume* vol, PfShard * s);

	void set_snap_seq(int64_t volume_id, int snap_seq);
	int set_meta_ver(int64_t volume_id, int meta_ver);
	int prepare_shards(PfVolume* vol);
};


 inline void PfServerIocb::dec_ref() {
	if (__sync_sub_and_fetch(&ref_count, 1) == 0) {
		free_to_pool();
	}
}

inline void PfServerIocb::re_init()
{
	complete_meta_ver = 0;
	complete_status = MSG_STATUS_SUCCESS;
	vol_id=0;
	is_timeout = FALSE;
	task_mask = 0;
}


/*
inline PfEventQueue* SubTask::half_complete(PfMessageStatus comp_status)
{
	complete_status = comp_status;
}
*/

#endif // pf_dispatcher_h__
