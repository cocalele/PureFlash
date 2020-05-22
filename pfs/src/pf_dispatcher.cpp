#include <pf_main.h>
#include "pf_dispatcher.h"
#include "pf_message.h"


//PfDispatcher::PfDispatcher(const std::string &name) :PfEventThread(name.c_str(), IO_POOL_SIZE*3) {
//
//}

int PfDispatcher::init(int disp_index)
{
 /*
 * Why use IO_POOL_SIZE * 3 for even_thread queue size?
 * a dispatcher may let max IO_POOL_SIZE  IOs in flying. For each IO, the following events will be posted to dispatcher:
 * 1. EVT_IO_REQ when a request was received complete (CMD for read, CMD + DATA for write op) from network
 * 2. EVT_IO_COMPLETE when a request was complete by each replica, there may be 3 data replica, and 1 remote replicating
 */
	int rc = PfEventThread::init(format_string("disp_%d", disp_index).c_str(), IO_POOL_SIZE * 4);
	if(rc)
		return rc;
	rc = init_mempools();
	return rc;
}

int PfDispatcher::prepare_volume(PfVolume* vol)
{
	if (opened_volumes.find(vol->id) != opened_volumes.end())
	{
		delete vol;
		return -EALREADY;
	}
	opened_volumes[vol->id] = vol;
	return 0;
}
int PfDispatcher::process_event(int event_type, int arg_i, void* arg_p)
{
	int rc = 0;
	switch(event_type) {
	case EVT_IO_REQ:
		rc = dispatch_io((PfServerIocb*)arg_p);
	}
	return rc;
}
int PfDispatcher::dispatch_io(PfServerIocb *iocb)
{
	pf_message_head* cmd = iocb->cmd_bd->cmd_bd;
	uint32_t shard_index = VOL_ID_TO_SHARD_INDEX(cmd->vol_id);
	PfShard * s = iocb->vol->shards[shard_index];
	iocb->task_mask = 0;
	for (int i = 0; i < iocb->vol->rep_count; i++) {
		if(s->replicas[i]->status == HS_OK) {
			iocb->task_mask |= iocb->subtasks[i]->task_mask;
			s->replicas[i]->submit_io(&iocb->io_subtasks[i]);
		}
	}
	return 0;
}
int PfDispatcher::init_mempools()
{
	int pool_size = IO_POOL_SIZE;
	int rc = 0;
	rc = cmd_pool.init(sizeof(pf_message_head), pool_size * 2);
	if (rc)
		goto release1;
	rc = data_pool.init(MAX_IO_SIZE, pool_size * 2);
	if (rc)
		goto release2;
	rc = reply_pool.init(sizeof(pf_message_reply), pool_size * 2);
	if (rc)
		goto release3;
	rc = iocb_pool.init(pool_size * 2);
	if(rc)
		goto release4;
	for(int i=0;i<pool_size;i++)
	{
		PfServerIocb *cb = iocb_pool.alloc();
		cb->cmd_bd = cmd_pool.alloc();
		cb->data_bd = data_pool.alloc();
		cb->reply_bd = reply_pool.alloc();
		for(int i=0;i<3;i++) {
			cb->subtasks[i] = &cb->io_subtasks[i];
			cb->subtasks[i]->rep_index =i;
			cb->subtasks[i]->task_mask = 1 << i;
		}
		//TODO: still 2 subtasks not initialized, for metro replicating and rebalance
		iocb_pool.free(cb);
	}
	return rc;
release4:
	reply_pool.destroy();
release3:
	data_pool.destroy();
release2:
	cmd_pool.destroy();
release1:
	return rc;
}

