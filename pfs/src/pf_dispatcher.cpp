/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <pf_main.h>
#include "pf_dispatcher.h"
#include "pf_message.h"
#include "pf_rdma_connection.h"

extern struct disp_mem_pool* disp_mem_pool[MAX_DISPATCHER_COUNT];
//PfDispatcher::PfDispatcher(const std::string &name) :PfEventThread(name.c_str(), IO_POOL_SIZE*3) {
//
//}

int PfDispatcher::init(int disp_index)
{
 /*
 * Why use IO_POOL_SIZE * 4 for even_thread queue size?
 * a dispatcher may let max IO_POOL_SIZE  IOs in flying. For each IO, the following events will be posted to dispatcher:
 * 1. EVT_IO_REQ when a request was received complete (CMD for read, CMD + DATA for write op) from network
 * 2. EVT_IO_COMPLETE when a request was complete by each replica, there may be 3 data replica, and 1 remote replicating
 */
    this->disp_index = disp_index;
	int rc = PfEventThread::init(format_string("disp_%d", disp_index).c_str(), IO_POOL_SIZE * 4);
	if(rc)
		return rc;
	rc = init_mempools(disp_index);
	return rc;
}

int PfDispatcher::prepare_volume(PfVolume* vol)
{
	assert(vol);
	auto pos = opened_volumes.find(vol->id);
	if (pos != opened_volumes.end())
	{
		PfVolume* old_v = pos->second;
		if(old_v->meta_ver >= vol->meta_ver) {
			S5LOG_WARN("Not update volume in dispatcher:%d, vol:%s, whose meta_ver:%d new meta_ver:%d",
			  disp_index, vol->name, old_v->meta_ver, vol->meta_ver);
			return 0;
		}

		old_v->meta_ver = vol->meta_ver;
		old_v->shard_count = vol->shard_count;
		old_v->size = vol->size;
		old_v->snap_seq = vol->snap_seq;
		old_v->status = vol->status;
		for(int i=0;i<old_v->shard_count;i++){
			PfShard *s=old_v->shards[i];
			for(int j=0;j<s->rep_count; j++) {
				if(s->replicas[i]->status == HealthStatus::HS_RECOVERYING && vol->shards[i]->replicas[j]->status == HealthStatus::HS_ERROR) {
					vol->shards[i]->replicas[j]->status = HealthStatus::HS_RECOVERYING; //keep recoverying continue
				}
			}
			old_v->shards[i] = vol->shards[i];
			vol->shards[i] = NULL;
			delete s;
		}
		for(int i=old_v->shard_count; i<vol->shards.size(); i++) { //enlarged shard
			old_v->shards.push_back(vol->shards[i]);
			vol->shards[i] = NULL;
		}
	} else {
		vol->add_ref();
		opened_volumes[vol->id] = vol;
	}
	return 0;
}
int PfDispatcher::process_event(int event_type, int arg_i, void* arg_p)
{
	int rc = 0;
	switch(event_type) {
	case EVT_IO_REQ:
		rc = dispatch_io((PfServerIocb*)arg_p);
		break;
	case EVT_IO_COMPLETE:
		rc = dispatch_complete((SubTask*)arg_p);
		break;
	default:
		S5LOG_FATAL("Unknown event:%d", event_type);
	}
	return rc;
}
static inline void reply_io_to_client(PfServerIocb *iocb)
{
	int rc = 0;
	const static int ms1 = 1000;
	if(unlikely(iocb->conn->state != CONN_OK)) {
		S5LOG_WARN("Give up to reply IO cid:%d on connection:%p:%s for state:%s", iocb->cmd_bd->cmd_bd->command_id,
			 iocb->conn, iocb->conn->connection_info.c_str(), ConnState2Str(iocb->conn->state));
		iocb->dec_ref();
		return;
	}
	uint64_t io_end_time = now_time_usec();
	uint64_t io_elapse_time = (io_end_time - iocb->received_time) / ms1;

	if (io_elapse_time > 2000)
	{
		S5LOG_WARN("SLOW IO, shard id:%ld, command_id:%d, vol:%s, since received:%dms",
		           iocb->cmd_bd->cmd_bd->offset >> SHARD_SIZE_ORDER,
		           iocb->cmd_bd->cmd_bd->command_id,
		           iocb->vol->name,
		           io_elapse_time
		);
	}
	PfMessageReply* reply_bd = iocb->reply_bd->reply_bd;
	PfMessageHead* cmd_bd = iocb->cmd_bd->cmd_bd;
	reply_bd->command_id = cmd_bd->command_id;
	reply_bd->status = iocb->complete_status;
	reply_bd->meta_ver = iocb->complete_meta_ver;
	reply_bd->command_seq = cmd_bd->command_seq;
	rc = iocb->conn->post_send(iocb->reply_bd);
	if (rc)
	{
		S5LOG_ERROR("post_send, rc:%d", rc);
	}
}
int PfDispatcher::dispatch_io(PfServerIocb *iocb)
{
	PfMessageHead* cmd = iocb->cmd_bd->cmd_bd;
	auto pos = opened_volumes.find(cmd->vol_id);

	if(unlikely(pos == opened_volumes.end())){
		S5LOG_ERROR("Cannot dispatch_io, op:%s, volume:0x%x not opened", PfOpCode2Str(cmd->opcode), cmd->vol_id);
		iocb->complete_status = PfMessageStatus::MSG_STATUS_REOPEN | PfMessageStatus::MSG_STATUS_INVALID_STATE;
		iocb->complete_meta_ver = -1;
		reply_io_to_client(iocb);
		return 0;
	}
	PfVolume* vol = pos->second;
	if(unlikely(cmd->meta_ver != vol->meta_ver))
	{
		S5LOG_ERROR("Cannot dispatch_io, op:%s(%d), volume:0x%x meta_ver:%d diff than client:%d",
			  PfOpCode2Str(cmd->opcode), cmd->opcode,  cmd->vol_id, vol->meta_ver, cmd->meta_ver);
		iocb->complete_status = PfMessageStatus::MSG_STATUS_REOPEN ;
		iocb->complete_meta_ver = (uint16_t)vol->meta_ver;
		reply_io_to_client(iocb);
		return 0;
	}
	//S5LOG_DEBUG("dispatch_io, op:%s, volume:%s ", PfOpCode2Str(cmd->opcode), vol->name);
	if(unlikely(cmd->snap_seq == SNAP_SEQ_HEAD))
		cmd->snap_seq = vol->snap_seq;

	uint32_t shard_index = (uint32_t)OFFSET_TO_SHARD_INDEX(cmd->offset);
	if(unlikely(shard_index > vol->shard_count)) {
		S5LOG_ERROR("Cannot dispatch_io, op:%s, volume:0x%x, offset:%lld exceeds volume size:%lld",
		            PfOpCode2Str(cmd->opcode), cmd->vol_id, cmd->offset, vol->size);
		iocb->complete_status = PfMessageStatus::MSG_STATUS_REOPEN | PfMessageStatus::MSG_STATUS_INVALID_FIELD;
		iocb->complete_meta_ver = (uint16_t)vol->meta_ver;
		reply_io_to_client(iocb);
		return 0;
	}
	PfShard * s = vol->shards[shard_index];

//	if(unlikely((cmd->offset & 0x0fff) || (cmd->length & 0x0fff)))	{
//		static int prt_cnt = 0;
//		prt_cnt ++;
//		if((prt_cnt % 1000) == 1) {
//			S5LOG_WARN("Unaligned IO on volume:%s OP:%s offset:0x%lx len:0x%x, num:%d", vol->name, PfOpCode2Str(cmd->opcode),
//			           cmd->offset, cmd->length, prt_cnt);
//		}
//	}
	switch(cmd->opcode) {
		case S5_OP_WRITE:
			return dispatch_write(iocb, vol, s);
			break;
		case S5_OP_READ:
		case S5_OP_RECOVERY_READ:
			return dispatch_read(iocb, vol, s);
			break;
		case S5_OP_REPLICATE_WRITE:
			return dispatch_rep_write(iocb, vol, s);
			break;
		default:
			S5LOG_FATAL("Unknown opcode:%s(%d)", PfOpCode2Str(cmd->opcode), cmd->opcode);

	}
	return 1;

}

int PfDispatcher::dispatch_write(PfServerIocb* iocb, PfVolume* vol, PfShard * s)
{
	PfMessageHead* cmd = iocb->cmd_bd->cmd_bd;
	iocb->task_mask = 0;
	if(unlikely(!s->is_primary_node || s->replicas[s->duty_rep_index]->status != HS_OK)){
		S5LOG_ERROR("Write on non-primary node, vol:0x%llx, %s, shard_index:%d, current replica_index:%d",
		            vol->id, vol->name, s->id, s->duty_rep_index);
		iocb->complete_status = PfMessageStatus::MSG_STATUS_REOPEN;
		reply_io_to_client(iocb);
		return 1;
	}
	iocb->setup_subtask(s, cmd->opcode);
	for (int i = 0; i < iocb->vol->rep_count; i++) {
		if(s->replicas[i]->status == HS_OK || s->replicas[i]->status == HS_RECOVERYING) {
			int rc = 0;
			iocb->subtasks[i]->rep_id = s->replicas[i]->id;
			iocb->subtasks[i]->store_id = s->replicas[i]->store_id;
			rc = s->replicas[i]->submit_io(&iocb->io_subtasks[i]);
			if(rc) {
				S5LOG_ERROR("submit_io, rc:%d", rc);
			}
		} else {
			S5LOG_DEBUG("process none");
		}
	}
	return 0;
}

int PfDispatcher::dispatch_read(PfServerIocb* iocb, PfVolume* vol, PfShard * s)
{
	PfMessageHead* cmd = iocb->cmd_bd->cmd_bd;

	iocb->task_mask = 0;
	int i = s->duty_rep_index;
	if(s->replicas[i]->status == HS_OK) {
		iocb->setup_one_subtask(s, i, cmd->opcode);

		iocb->subtasks[i]->rep_id = s->replicas[i]->id;
		iocb->subtasks[i]->store_id = s->replicas[i]->store_id;
		if(unlikely(!s->is_primary_node)) {
			S5LOG_ERROR("Read on non-primary node, vol:0x%llx, %s, shard_index:%d, current replica_index:%d",
			            vol->id, vol->name, s->id, s->duty_rep_index);
			app_context.error_handler->submit_error((IoSubTask*)iocb->subtasks[i], PfMessageStatus::MSG_STATUS_NOT_PRIMARY);
			return 1;
		}
		s->replicas[i]->submit_io(&iocb->io_subtasks[i]);
	} else {
		S5LOG_ERROR("replica:0x%x status:%s not readable", s->replicas[i]->id, HealthStatus2Str(s->replicas[i]->status));
	}

	return 0;
}

int PfDispatcher::dispatch_rep_write(PfServerIocb* iocb, PfVolume* vol, PfShard * s)
{
	//PfMessageHead* cmd = iocb->cmd_bd->cmd_bd;

	iocb->task_mask = 0;
	int i = s->duty_rep_index;
	if(likely(s->replicas[i]->status == HS_OK) || s->replicas[i]->status == HS_RECOVERYING) {
		iocb->setup_one_subtask(s, i, PfOpCode::S5_OP_REPLICATE_WRITE);
		iocb->subtasks[i]->rep_id = s->replicas[i]->id;
		iocb->subtasks[i]->store_id = s->replicas[i]->store_id;
		if(unlikely(s->is_primary_node)) {
			S5LOG_ERROR("Repwrite on primary node, vol:0x%llx, %s, shard_index:%d, current replica_index:%d",
			            vol->id, vol->name, s->id, s->duty_rep_index);
			app_context.error_handler->submit_error((IoSubTask*)iocb->subtasks[i], PfMessageStatus::MSG_STATUS_REP_TO_PRIMARY);
			return 1;
		}
		s->replicas[i]->submit_io(&iocb->io_subtasks[i]);
	} else {
		S5LOG_FATAL("Unexpected replica status:%d on replicating write, rep:0x%llx" , s->replicas[i]->status, s->replicas[i]->id);
	}

	return 0;
}


int PfDispatcher::dispatch_complete(SubTask* sub_task)
{
	PfServerIocb* iocb = sub_task->parent_iocb;
//	S5LOG_DEBUG("complete subtask:%p, status:%d, task_mask:0x%x, parent_io mask:0x%x, io_cid:%d", sub_task, sub_task->complete_status,
//			sub_task->task_mask, iocb->task_mask, iocb->cmd_bd->cmd_bd->command_id);
	iocb->task_mask &= (~sub_task->task_mask);
	iocb->complete_status = (iocb->complete_status == PfMessageStatus::MSG_STATUS_SUCCESS ? sub_task->complete_status : iocb->complete_status);
	iocb->dec_ref(); //added in setup_subtask
	if(iocb->task_mask == 0){
        PfRdmaConnection *conn = (PfRdmaConnection *)iocb->conn;
        if (IS_READ_OP(iocb->cmd_bd->cmd_bd->opcode) && (conn->transport == TRANSPORT_RDMA)) {
            iocb->add_ref();
            conn->add_ref();
            int rc = conn->post_write(iocb->data_bd, iocb->cmd_bd->cmd_bd->buf_addr, iocb->cmd_bd->cmd_bd->rkey);
            if (rc)
            {
            	S5LOG_ERROR("post_write, rc:%d", rc);
            }
        }
		reply_io_to_client(iocb);
	}
	return 0;
}

int PfDispatcher::init_mempools(int disp_index)
{
	int pool_size = IO_POOL_SIZE;
	int rc = 0;
	rc = mem_pool.cmd_pool.init(sizeof(PfMessageHead), pool_size * 2);
	if (rc)
		goto release1;
	S5LOG_INFO("Allocate data_pool with max IO size:%d, depth:%d", PF_MAX_IO_SIZE, pool_size * 2);
	rc = mem_pool.data_pool.init(PF_MAX_IO_SIZE, pool_size * 2);
	if (rc)
		goto release2;
	rc = mem_pool.reply_pool.init(sizeof(PfMessageReply), pool_size * 2);
	if (rc)
		goto release3;
	rc = iocb_pool.init(pool_size * 2);
	if(rc)
		goto release4;
	for(int i=0;i<pool_size*2;i++)
	{
		PfServerIocb *cb = iocb_pool.alloc();
		cb->cmd_bd = mem_pool.cmd_pool.alloc();
		cb->cmd_bd->data_len = sizeof(PfMessageHead);
		cb->cmd_bd->server_iocb = cb;
		cb->data_bd = mem_pool.data_pool.alloc();
		//data len of data_bd depends on length in message head
		cb->data_bd->server_iocb = cb;
		cb->disp_index = disp_index;
		cb->reply_bd = mem_pool.reply_pool.alloc();
		cb->reply_bd->data_len =  sizeof(PfMessageReply);
		cb->reply_bd->server_iocb = cb;
		for(int i=0;i<3;i++) {
			cb->subtasks[i] = &cb->io_subtasks[i];
			cb->subtasks[i]->rep_index =i;
			cb->subtasks[i]->task_mask = 1 << i;
			cb->subtasks[i]->parent_iocb = cb;
		}

		//TODO: still 2 subtasks not initialized, for metro replicating and rebalance
		iocb_pool.free(cb);
	}
#ifdef WITH_RDMA
	disp_mem_pool[disp_index] = &mem_pool;
#endif
	return rc;
release4:
	mem_pool.reply_pool.destroy();
release3:
	mem_pool.data_pool.destroy();
release2:
	mem_pool.cmd_pool.destroy();
release1:
	return rc;
}

void PfDispatcher::set_snap_seq(int64_t volume_id, int snap_seq) {
	auto pos = opened_volumes.find(volume_id);
	if(pos == opened_volumes.end()){
		S5LOG_ERROR("Volume:0x%llx not found in dispatcher:%s", volume_id, name);
		return;
	}
	pos->second->snap_seq = snap_seq;
}

int PfDispatcher::set_meta_ver(int64_t volume_id, int meta_ver) {
	auto pos = opened_volumes.find(volume_id);
	if(pos == opened_volumes.end()){
		S5LOG_ERROR("Volume:0x%llx not found in dispatcher:%s", volume_id, name);
		return -ENOENT;
	}
	if(meta_ver < pos->second->meta_ver){
		S5LOG_ERROR("Refuse to update voluem:%s meta_ver from:%d to %d", pos->second->name, pos->second->meta_ver, meta_ver);
		return -EINVAL;
	}
	pos->second->meta_ver = meta_ver;
	return 0;
}