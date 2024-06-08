/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <pf_main.h>
#include "pf_dispatcher.h"
#include "pf_message.h"
#include "pf_rdma_connection.h"
#include "pf_trace_defs.h"
#include "spdk/trace.h"
#include "spdk/env.h"
//PfDispatcher::PfDispatcher(const std::string &name) :PfEventThread(name.c_str(), IO_POOL_SIZE*3) {
//
//}
 
int PfDispatcher::init(int disp_index, uint16_t* p_id)
{
 /*
 * Why use IO_POOL_SIZE * 4 for even_thread queue size?
 * a dispatcher may let max IO_POOL_SIZE  IOs in flying. For each IO, the following events will be posted to dispatcher:
 * 1. EVT_IO_REQ when a request was received complete (CMD for read, CMD + DATA for write op) from network
 * 2. EVT_IO_COMPLETE when a request was complete by each replica, there may be 3 data replica, and 1 remote replicating
 */
    this->disp_index = disp_index;
	int rc = PfEventThread::init(format_string("disp_%d", disp_index).c_str(), IO_POOL_SIZE * 4, *p_id);
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

		(*old_v) = std::move(*vol);
	} else {
		vol->add_ref();
		opened_volumes[vol->id] = vol;
	}
	return 0;
}

int PfDispatcher::delete_volume(uint64_t  vol_id)
{
	assert(vol_id);
	auto pos = opened_volumes.find(vol_id);
	if (pos != opened_volumes.end())
	{
		PfVolume* vol = pos->second;
		vol->dec_ref();
		opened_volumes.erase(pos);
	}
	return 0;
}

int PfDispatcher::process_event(int event_type, int arg_i, void* arg_p, void*)
{
	int rc = 0;
	switch(event_type) {
	case EVT_IO_REQ:
		rc = dispatch_io((PfServerIocb*)arg_p);
		break;
	case EVT_IO_COMPLETE:
		rc = dispatch_complete((SubTask*)arg_p);
		break;
	case EVT_FORCE_RELEASE_CONN:
		{
			PfRdmaConnection* c = (PfRdmaConnection*)arg_p;
			if(c->conn_type != RDMA_TYPE){
				c->dec_ref();//added before this EVENT send
				return 0;
			}
			
			S5LOG_DEBUG("Check conn:%p ref_cnt:%d", c, c->ref_count);
			int i=0;
			for (PfServerIocb* iocb : c->used_iocb) {
				if (iocb->conn == c) {
					iocb->dec_ref_on_error();
					i++;
				}
			}
			S5LOG_DEBUG("%d iocb released", i);
			c->dec_ref();
			return 0;
		}
	default:
		S5LOG_ERROR("Unknown event:%d", event_type);
	}
	return rc;
}

static inline void reply_io_to_client(PfServerIocb *iocb)
{
	int rc = 0;
	const static int ms1 = 1000;
	PfConnection* conn = iocb->conn;
	if(unlikely(iocb->conn == NULL || iocb->conn->state != CONN_OK)) {
		if(iocb->conn){
		S5LOG_WARN("Give up to reply IO cid:%d on connection:%p:%s for state:%s", iocb->cmd_bd->cmd_bd->command_id,
			 iocb->conn, iocb->conn->connection_info.c_str(), ConnState2Str(iocb->conn->state));
		} else {
			S5LOG_WARN("Give up to reply IO cid:%d for connection is NULL", iocb->cmd_bd->cmd_bd->command_id);
		}
		iocb->dec_ref_on_error();
		return;
	}

	uint64_t io_end_time = now_time_usec();
	uint64_t io_elapse_time = (io_end_time - iocb->received_time) / ms1;

	if (io_elapse_time > 2000)
	{
		S5LOG_WARN("SLOW IO, shard id:%ld, command_id:%d, vol_id:0x%lx, since received:%dms",
		           iocb->cmd_bd->cmd_bd->offset >> SHARD_SIZE_ORDER,
		           iocb->cmd_bd->cmd_bd->command_id,
		           iocb->vol_id,
		           io_elapse_time
		);
	}

	if (IS_READ_OP(iocb->cmd_bd->cmd_bd->opcode) && (conn->conn_type == RDMA_TYPE) && iocb->complete_status == MSG_STATUS_SUCCESS) {
		//iocb->add_ref(); //iocb still have a valid ref, until reply sending complete
		//S5LOG_INFO("rdma post write!!!,ref_count:%d", iocb->ref_count);
		int rc = ((PfRdmaConnection*)conn)->post_write(iocb->data_bd, iocb->cmd_bd->cmd_bd->buf_addr, iocb->cmd_bd->cmd_bd->rkey);
		if (rc)
		{
			iocb->dec_ref_on_error();
			S5LOG_ERROR("Failed to post_write, rc:%d", rc);
			return;
		}
		//continue to send reply, RDMA can ensure the data reach before reply
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
		iocb->dec_ref_on_error();
		S5LOG_ERROR("Failed to post_send, rc:%d", rc);
	}
}

int PfDispatcher::dispatch_io(PfServerIocb *iocb)
{
	PfMessageHead* cmd = iocb->cmd_bd->cmd_bd;
	auto pos = opened_volumes.find(cmd->vol_id);

	if (unlikely(pos == opened_volumes.end())) {
		S5LOG_ERROR("Cannot dispatch_io, op:%s, volume:0x%x not opened", PfOpCode2Str(cmd->opcode), cmd->vol_id);
		iocb->complete_status = PfMessageStatus::MSG_STATUS_REOPEN | PfMessageStatus::MSG_STATUS_INVALID_STATE;
		iocb->complete_meta_ver = -1;
		reply_io_to_client(iocb);
		return 0;
	}

	PfVolume* vol = pos->second;
	if (unlikely(cmd->meta_ver != vol->meta_ver)) {
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
			stat.wr_cnt++;
			stat.wr_bytes += cmd->length;
			return dispatch_write(iocb, vol, s);
			break;
		case S5_OP_READ:
		case S5_OP_RECOVERY_READ:
			stat.rd_cnt++;
			stat.rd_bytes += cmd->length;
			return dispatch_read(iocb, vol, s);
			break;
		case S5_OP_REPLICATE_WRITE:
			stat.rep_wr_cnt++;
			stat.rep_wr_bytes += cmd->length;
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
	if (unlikely(!s->is_primary_node || s->replicas[s->duty_rep_index]->status != HS_OK)) {
		S5LOG_ERROR("Write on non-primary node, vol:0x%llx, %s, shard_index:%d, current replica_index:%d status:%d cid:%d",
		            vol->id, vol->name, s->shard_index, s->duty_rep_index, s->replicas[s->duty_rep_index]->status, iocb->cmd_bd->cmd_bd->command_id);
		int i = s->duty_rep_index;
		iocb->setup_one_subtask(s, i, cmd->opcode);
		iocb->subtasks[i]->rep_id = s->replicas[i]->id;
		iocb->subtasks[i]->store_id = s->replicas[i]->store_id;
		app_context.error_handler->submit_error((IoSubTask*)iocb->subtasks[i], PfMessageStatus::MSG_STATUS_NOT_PRIMARY);
		return 1;
	}
	iocb->setup_subtask(s, cmd->opcode);
#ifdef WITH_SPDK_TRACE
	iocb->submit_rep_time = spdk_get_ticks();
	iocb->primary_rep_index = s->primary_replica_index;
	iocb->local_cost_time = 0;
	iocb->remote_rep1_cost_time = 0;
	iocb->remote_rep2_cost_time = 0;
	iocb->remote_rep1_submit_cost = 0;
	iocb->remote_rep2_submit_cost = 0;
        iocb->remote_rep1_reply_cost = 0;
        iocb->remote_rep2_reply_cost = 0;
#endif
	for (int i = 0; i < vol->rep_count; i++) {
		if (s->replicas[i]->status == HS_OK || s->replicas[i]->status == HS_RECOVERYING) {
			int rc = 0;
			iocb->subtasks[i]->rep_id = s->replicas[i]->id;
			iocb->subtasks[i]->store_id = s->replicas[i]->store_id;
			rc = s->replicas[i]->submit_io(&iocb->io_subtasks[i]);
			if (rc) {
				S5LOG_ERROR("submit_io, rc:%d", rc);
			}
		} else {
			//S5LOG_DEBUG("process none");
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
		if(unlikely(!s->is_primary_node || s->replicas[s->duty_rep_index]->status != HS_OK)) {
			S5LOG_ERROR("Read on non-primary node, vol:0x%llx, %s, shard_index:%d, current replica_index:%d status:%d",
			            vol->id, vol->name, s->id, s->duty_rep_index, s->replicas[s->duty_rep_index]->status);
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
#ifdef WITH_SPDK_TRACE
	iocb->submit_rep_time = spdk_get_ticks();
	iocb->primary_rep_index = s->primary_replica_index;
	iocb->local_cost_time = 0;
	iocb->remote_rep1_cost_time = 0;
	iocb->remote_rep2_cost_time = 0;
	iocb->remote_rep1_submit_cost = 0;
	iocb->remote_rep2_submit_cost = 0;
        iocb->remote_rep1_reply_cost = 0;
        iocb->remote_rep2_reply_cost = 0;
#endif
	if (likely(s->replicas[i]->status == HS_OK) || s->replicas[i]->status == HS_RECOVERYING) {
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

static void server_complete(SubTask* t, PfMessageStatus comp_status) {
	t->complete_status = comp_status;
	//assert(((PfServerIocb*)t->parent_iocb)->ref_count ); // Ϊ0 �����ǲ�Ӧ�õġ�

	app_context.disps[((PfServerIocb*)t->parent_iocb)->disp_index]->event_queue->post_event(EVT_IO_COMPLETE, 0, t);
}
static void server_complete_with_metaver(SubTask* t, PfMessageStatus comp_status, uint16_t meta_ver) {
	if (meta_ver > ((PfServerIocb*)t->parent_iocb)->complete_meta_ver)
		((PfServerIocb*)t->parent_iocb)->complete_meta_ver = meta_ver;
	server_complete(t, comp_status);
}
static struct TaskCompleteOps _server_task_complete_ops={ server_complete , server_complete_with_metaver };

int PfDispatcher::dispatch_complete(SubTask* sub_task)
{
	PfServerIocb* iocb = (PfServerIocb * )sub_task->parent_iocb;
//	S5LOG_DEBUG("complete subtask:%p, status:%d, task_mask:0x%x, parent_io mask:0x%x, io_cid:%d", sub_task, sub_task->complete_status,
//			sub_task->task_mask, iocb->task_mask, iocb->cmd_bd->cmd_bd->command_id);
#ifdef WITH_SPDK_TRACE
	uint64_t rep_io_complete_tsc = spdk_get_ticks();
	uint64_t cost = get_us_from_tsc(rep_io_complete_tsc - iocb->submit_rep_time, get_current_thread()->tsc_rate);
        uint64_t rep_submit_cost = get_us_from_tsc(sub_task->submit_time - iocb->received_time_hz, get_current_thread()->tsc_rate);
        uint64_t rep_reply_cost = get_us_from_tsc(rep_io_complete_tsc - sub_task->reply_time, get_current_thread()->tsc_rate);
	// local primary shard io is finished
	if ((sub_task->task_mask >> iocb->primary_rep_index) == 1) {
		iocb->local_cost_time = cost;
	} else {
		// non primary shard io is finished
		if (iocb->remote_rep1_cost_time == 0) {
			iocb->remote_rep1_cost_time = cost;
		} else if (iocb->remote_rep2_cost_time == 0) {
			iocb->remote_rep2_cost_time = cost;
		}

                if (iocb->remote_rep1_submit_cost == 0) {
                        iocb->remote_rep1_submit_cost = rep_submit_cost;
                } else if (iocb->remote_rep2_submit_cost == 0) {
                        iocb->remote_rep2_submit_cost = rep_submit_cost;
                }
                if (iocb->remote_rep1_reply_cost == 0) {
                        iocb->remote_rep1_reply_cost = rep_reply_cost;
                } else if (iocb->remote_rep2_reply_cost == 0) {
                        iocb->remote_rep2_reply_cost = rep_reply_cost;
                }                        

	}
#endif
	iocb->task_mask &= (~sub_task->task_mask);
	iocb->complete_status = (iocb->complete_status == PfMessageStatus::MSG_STATUS_SUCCESS ? sub_task->complete_status : iocb->complete_status);

	if(iocb->task_mask == 0) {
		// all rep io finish
	#ifdef WITH_SPDK_TRACE
		spdk_poller_trace_record(TRACE_DISP_IO_STAT, get_current_thread()->poller_id, 0,
                                         iocb->cmd_bd->cmd_bd->offset, iocb->local_cost_time, 
			                 iocb->remote_rep1_cost_time, iocb->remote_rep2_cost_time,
			                 get_us_from_tsc(rep_io_complete_tsc - iocb->received_time_hz, get_current_thread()->tsc_rate));
                spdk_poller_trace_record(TRACE_DISP_REP_IO_STAT, get_current_thread()->poller_id, 0,
                                         iocb->cmd_bd->cmd_bd->offset, iocb->remote_rep1_submit_cost, 
			                 iocb->remote_rep2_submit_cost, iocb->remote_rep1_reply_cost, iocb->remote_rep2_reply_cost);
	#endif
		reply_io_to_client(iocb);
	}
	iocb->dec_ref(); //added in setup_subtask. In most case, should never dec to 0
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
	if (spdk_engine_used())
		mem_pool.data_pool.dma_buffer_used = true;
	rc = mem_pool.data_pool.init(PF_MAX_IO_SIZE, pool_size * 2);
	if (rc)
		goto release2;
	rc = mem_pool.reply_pool.init(sizeof(PfMessageReply), pool_size * 2);
	if (rc)
		goto release3;
	rc = iocb_pool.init(pool_size * 2);
	if (rc)
		goto release4;
	for (int i = 0; i < pool_size * 2; i++)
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
		for (int i = 0; i < PF_MAX_SUBTASK_CNT; i++) {
			cb->subtasks[i] = &cb->io_subtasks[i];
			cb->subtasks[i]->rep_id = -1ULL;//re-set before dispatch
			cb->subtasks[i]->rep_index = i;
			cb->subtasks[i]->task_mask = 1 << i;
			cb->subtasks[i]->parent_iocb = cb;
			cb->subtasks[i]->ops = &_server_task_complete_ops;
		}

		//TODO: still 2 subtasks not initialized, for metro replicating and rebalance
		iocb_pool.free(cb);
	}
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

int PfDispatcher::prepare_shards(PfVolume* vol)
{
	assert(vol);
	auto pos = opened_volumes.find(vol->id);
	if (pos == opened_volumes.end()) {
		S5LOG_ERROR("Volume:%s not opened and can't add tempory replica", vol->name);
		return -ENOENT;
	}

	PfVolume* old_v = pos->second;
	old_v->meta_ver = vol->meta_ver;
	for(int i=0;i<vol->shards.size();i++)
	{
		PfShard* new_shard = vol->shards[i];
		PfShard* old_shard = old_v->shards[new_shard->shard_index];
		old_v->shards[new_shard->shard_index] = new_shard;
		vol->shards[i] = NULL;
		delete old_shard;
	}
	return 0;
}


void PfServerIocb::free_to_pool()
{
	//S5LOG_DEBUG("Iocb released:%p", this);
	PfConnection* conn_tmp = conn;
	complete_meta_ver = 0;
	complete_status = MSG_STATUS_SUCCESS;
	vol_id = 0;
	is_timeout = FALSE;
	task_mask = 0;
	conn = NULL;
	if (conn_tmp != NULL) {

		conn_tmp->dec_ref();
	}
	assert(disp_index >= 0 && disp_index < app_context.disps.size());
	app_context.disps[disp_index]->iocb_pool.free(this);
}

void PfServerIocb::dec_ref_on_error() {
	if (__sync_sub_and_fetch(&ref_count, 1) == 0) {
		free_to_pool();
	}
	else {
		PfConnection* conn_tmp = conn;
		if (conn_tmp) {
			conn = NULL;
			conn_tmp->dec_ref();
		}
	}

}

SPDK_TRACE_REGISTER_FN(disp_trace, "disp", TRACE_GROUP_DISP)
{
	struct spdk_trace_tpoint_opts opts[] = {
        {
			"DISP_IO_STAT", TRACE_DISP_IO_STAT,
			OWNER_PFS_DISP_IO, OBJECT_DISP_IO, 1,
			{
				{ "lcost", SPDK_TRACE_ARG_TYPE_INT, 8},
				{ "r1cost", SPDK_TRACE_ARG_TYPE_INT, 8},
				{ "r2cost", SPDK_TRACE_ARG_TYPE_INT, 8},
				{ "cost", SPDK_TRACE_ARG_TYPE_INT, 8}
			}
	},
        {
			"DISP_REP_IO_STAT", TRACE_DISP_REP_IO_STAT,
			OWNER_PFS_DISP_IO, OBJECT_DISP_IO, 2,
			{
				{ "r1sc", SPDK_TRACE_ARG_TYPE_INT, 8},
				{ "r2sc", SPDK_TRACE_ARG_TYPE_INT, 8},
				{ "r1rc", SPDK_TRACE_ARG_TYPE_INT, 8},
                                { "r2rc", SPDK_TRACE_ARG_TYPE_INT, 8}
			}
	},
	};


	spdk_trace_register_owner(OWNER_PFS_DISP_IO, 'd');
	spdk_trace_register_object(OBJECT_DISP_IO, 'd');
	spdk_trace_register_description_ext(opts, SPDK_COUNTOF(opts));
}