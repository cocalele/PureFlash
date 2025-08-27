/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <unistd.h>
#include <pf_tcp_connection.h>

#include "pf_dispatcher.h"
#include "pf_replicator.h"
#include "pf_client_priv.h"
#include "pf_message.h"
#include "pf_main.h"
#include "spdk/env.h"
#ifdef WITH_RDMA
#include "pf_rdma_connection.h"
#endif

#define USE_DELAY_THREAD

extern enum connection_type rep_conn_type;

int PfDelayThread::process_event(int event_type, int arg_i, void* arg_p, void* arg_q)
{
	if(event_type == EVT_IO_REQ){
		int64_t delta = now_time_usec() - (int64_t)arg_q;
		if(delta > 0){
			usleep((int)delta);
		} 

		replicator->event_queue->post_event(EVT_IO_REQ, 0, arg_p);
	}
	else {
		S5LOG_ERROR("Delay thread get unknow event");
	}
	return 0;
}

int PfReplicator::begin_replicate_io(IoSubTask* t)
{
	int rc;
	PfConnection* c = (PfConnection*)conn_pool->get_conn((int)t->store_id);
	if (unlikely(c == NULL)) {
		S5LOG_ERROR("Failed get connection to store:%d", t->store_id);
		PfMessageHead* cmd = t->parent_iocb->cmd_bd->cmd_bd;
		PfVolume* vol = app_context.get_opened_volume(cmd->vol_id);
		uint32_t shard_index = (uint32_t)OFFSET_TO_SHARD_INDEX(cmd->offset);
		PfShard* s = vol->shards[shard_index];
		PfReplica * r = s->replicas[t->rep_index];
		if( r->status == HS_RECOVERYING){
			r->status = HS_ERROR;
			S5LOG_ERROR("Stop recovery on replica:0x%lx rep_index:%d for connection error", r->id, t->rep_index);//we may have dup replica id, but always unique index
			//TODO: stop ongoing recovery job on this replica
		}
		app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_CONN_LOST);
		return PfMessageStatus::MSG_STATUS_CONN_LOST;
	}
	if(!c->get_throttle()){
		S5LOG_ERROR("too many request to get throttle on conn:%p, retry later", c);
#ifdef USE_DELAY_THREAD
		delay_thread.event_queue->post_event(EVT_IO_REQ, 0, t, (void*)(now_time_usec() + 100));
#else
		usleep(100);
		event_queue->post_event(EVT_IO_REQ, 0, t);
#endif		
		return -EAGAIN;

	}
	PfClientIocb* io = iocb_pool.alloc();
	if (unlikely(io == NULL)) {
		S5LOG_ERROR("too many request to alloc IOCB for replicating, retry later");
#ifdef USE_DELAY_THREAD
		delay_thread.event_queue->post_event(EVT_IO_REQ, 0, t, (void*)(now_time_usec() + 100));
#else
		usleep(100);
		event_queue->post_event(EVT_IO_REQ, 0, t);
#endif
		return -EAGAIN;
	}
	io->submit_time = now_time_usec();
	auto old_cid = io->cmd_bd->cmd_bd->command_id;
	memcpy(io->cmd_bd->cmd_bd, t->parent_iocb->cmd_bd->cmd_bd, sizeof(PfMessageHead));
	io->cmd_bd->cmd_bd->command_id = old_cid;
	assert(io->cmd_bd->cmd_bd->opcode == PfOpCode::S5_OP_WRITE);
	assert(t->opcode == PfOpCode::S5_OP_WRITE || t->opcode == PfOpCode::S5_OP_REPLICATE_WRITE);
	t->opcode = PfOpCode::S5_OP_REPLICATE_WRITE; //setup_task has set opcode to OP_WRITE
	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;

	io->user_buf = NULL;
	io->data_bd = t->parent_iocb->data_bd;
	io->data_bd->cbk_data = io;
	io->ulp_arg = t;
	cmd->opcode = t->opcode;
	cmd->buf_addr = (__le64) io->data_bd->buf;
	io->conn = c;
#ifdef WITH_RDMA
	if(conn_pool->conn_type == RDMA_TYPE)
		cmd->rkey = io->data_bd->mrs[((PfRdmaConnection *)c)->dev_ctx->idx]->rkey;
#endif
	BufferDescriptor* rbd = mem_pool.reply_pool.alloc();
	if(unlikely(rbd == NULL))
	{
		S5LOG_ERROR("replicator[%d] has no recv_bd available now, requeue IO", rep_index);
		usleep(100);
		event_queue->post_event(EVT_IO_REQ, 0, t); //requeue the request
		io->conn = NULL;
		iocb_pool.free(io);
		return -EAGAIN;
	}
	rc = c->post_recv(rbd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed to post_recv in replicator[%d], connection:%s, rc:%d", rep_index, c->connection_info.c_str(), rc);
		usleep(100);
		event_queue->post_event(EVT_IO_REQ, 0, t); //requeue the request
		mem_pool.reply_pool.free(rbd);
		io->conn = NULL;
		iocb_pool.free(io);
		return -EAGAIN;
	}
	io->reply_bd = rbd;
	rc = c->post_send(io->cmd_bd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed to post_send in replicator[%d], connection:%s, rc:%d", rep_index, c->connection_info.c_str(), rc);
		usleep(100);
		event_queue->post_event(EVT_IO_REQ, 0, t); //requeue the request
		//cann't free reply bd, since it was post into connection
		io->conn = NULL;
		iocb_pool.free(io);
		return -EAGAIN;
	}
	t->submit_time = spdk_get_ticks();
	return rc;
}

int PfReplicator::process_io_complete(PfClientIocb* iocb, int _complete_status)
{
	(void)_complete_status;
	PfConnection* conn = iocb->conn;
	PfMessageReply *reply = iocb->reply_bd->reply_bd;
	PfMessageHead* io_cmd = iocb->cmd_bd->cmd_bd;
	uint64_t ms1 = 1000;

	iocb->reply_time = now_time_usec();
	uint64_t io_elapse_time = (iocb->reply_time - iocb->submit_time) / ms1;
	if (unlikely(io_elapse_time > 2000))
	{
		S5LOG_WARN("SLOW IO, shard id:%d, command_id:%d, op:%s, since submit:%ulms since send:%ulms",
				   io_cmd->offset >> SHARD_SIZE_ORDER,
				   io_cmd->command_id,
				   PfOpCode2Str(io_cmd->opcode),
				   io_elapse_time,
				   (iocb->reply_time-iocb->sent_time)/ms1
		);
	}
	PfMessageStatus s = (PfMessageStatus)reply->status;

	//On client side, we rely on the io timeout mechanism to release time connection
	//Here we just release the io task
	if (unlikely(io_cmd->opcode == S5_OP_HEARTBEAT))
	{
		__sync_fetch_and_sub(&conn->inflying_heartbeat, 1);
	} else {
		//if(io_cmd->opcode == S5_OP_RECOVERY_READ) {
		//	S5LOG_INFO("Recovery cid:%d complete", io_cmd->command_id);
		//}
		SubTask *t = (SubTask *) iocb->ulp_arg;
		t->ops->complete_meta_ver(t, s, reply->meta_ver);
	}

	mem_pool.reply_pool.free(iocb->reply_bd);
	iocb->reply_bd = NULL;
	iocb->data_bd = NULL; //replicator has no data bd pool, data_bd comes from up layer
	iocb->conn = NULL;
	iocb_pool.free(iocb);

	return 0;
}

int PfReplicator::begin_recovery_read_io(RecoverySubTask* t)
{
	int rc;
	PfClientIocb* io = iocb_pool.alloc();
	if (unlikely(io == NULL)) {
		S5LOG_ERROR("Failed to allock IOCB for recovery read");
		t->ops->complete(t, PfMessageStatus::MSG_STATUS_NO_RESOURCE);
		return -EAGAIN;
	}
	io->submit_time = now_time_usec();
	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;

	io->user_buf = NULL;
	io->data_bd = t->recovery_bd;
	io->data_bd->client_iocb = io;
	io->data_bd->cbk_data = io;
	io->ulp_arg = t;
	cmd->opcode = t->opcode;
	cmd->buf_addr = (__le64) io->data_bd->buf;
	cmd->vol_id = t->volume_id;
	cmd->rkey = 0;
	cmd->offset = t->offset;
	cmd->length = (uint32_t)t->length;
	cmd->snap_seq = t->snap_seq;
	cmd->meta_ver = t->meta_ver;

	PfConnection* c = conn_pool->get_conn((int)t->store_id);
	if(c == NULL) {
		S5LOG_ERROR("Failed get connection to store:%d for  recovery read ", t->store_id);
		t->ops->complete(t, PfMessageStatus::MSG_STATUS_CONN_LOST);
		iocb_pool.free(io);
		return -EINVAL;
	}
	io->conn = c;
#ifdef WITH_RDMA
	if(conn_pool->conn_type == RDMA_TYPE)
		cmd->rkey = io->data_bd->mrs[((PfRdmaConnection *)c)->dev_ctx->idx]->rkey;
#endif
	BufferDescriptor* rbd = mem_pool.reply_pool.alloc();
	if(unlikely(rbd == NULL))
	{
		S5LOG_ERROR("replicator[%d] has no recv_bd available now, abort recovery read.", rep_index);
		t->ops->complete(t, PfMessageStatus::MSG_STATUS_NO_RESOURCE);
		io->conn = NULL;
		iocb_pool.free(io);
		return -EAGAIN;
	}
	rc = c->post_recv(rbd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed to post_recv in replicator[%d], connection:%s, rc:%d for recovery read", rep_index, c->connection_info.c_str(), rc);
		t->ops->complete(t, PfMessageStatus::MSG_STATUS_NO_RESOURCE);
		mem_pool.reply_pool.free(rbd);
		io->conn = NULL;
		iocb_pool.free(io);
		return -EAGAIN;
	}
	io->reply_bd = rbd;
	rc = c->post_send(io->cmd_bd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed to post_send in replicator[%d], connection:%s, rc:%d for recovery read", rep_index, c->connection_info.c_str(), rc);
		t->ops->complete(t, PfMessageStatus::MSG_STATUS_NO_RESOURCE);
		//cann't free reply bd, since it was post into connection
		io->conn = NULL;
		iocb_pool.free(io);
		return -EAGAIN;
	}
	//S5LOG_DEBUG("Send replicating read request, cid:%d", io->cmd_bd->cmd_bd->command_id);
	return rc;
}

int PfReplicator::process_event(int event_type, int arg_i, void *arg_p, void*)
{
	switch(event_type) {
		case EVT_IO_REQ:
			return begin_replicate_io((IoSubTask*)arg_p);
			break;
		case EVT_IO_COMPLETE:
			return process_io_complete((PfClientIocb*)arg_p, arg_i);
			break;
		case EVT_RECOVERY_READ_IO:
			return begin_recovery_read_io((RecoverySubTask*) arg_p);
			break;
		case EVT_CONN_CLOSED:
			return handle_conn_close((PfConnection*) arg_p);
			break;
		default:
			S5LOG_FATAL("Unknown event_type:%d in replicator", event_type);

	}
	return 0;
}

void PfReplicator::PfRepConnectionPool::add_peer(int store_id, std::string ip1, std::string ip2)
{
	PeerAddr a = {.store_id=store_id, .conn=NULL, .curr_ip_idx=0, .ip={ip1, ip2} };
	peers[store_id] = a;
}

void PfReplicator::PfRepConnectionPool::connect_peer(int store_id)
{
	get_conn(store_id);
}

PfConnection* PfReplicator::PfRepConnectionPool::get_conn(int store_id)
{
	auto pos = peers.find(store_id);
	if (unlikely(pos == peers.end())) {
		S5LOG_ERROR("Peer IP for store_id:%d not found", store_id);
		return NULL;
	}
	PeerAddr& addr = pos->second;
	if (addr.conn != NULL && addr.conn->state == CONN_OK)
		return addr.conn;
	for (int i = 0; i < addr.ip.size(); i++) {
		if(addr.ip[addr.curr_ip_idx].size() > 0){ //an empty string may added as placeholder in prepare volume
			addr.conn = PfConnectionPool::get_conn(addr.ip[addr.curr_ip_idx], conn_type);
			if(addr.conn)
				return addr.conn;
		}
		addr.curr_ip_idx = (addr.curr_ip_idx+1)%(int)addr.ip.size();
	}
	return NULL;
}

static int replicator_on_tcp_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfTcpConnection* conn = (PfTcpConnection*)_conn;
	if(complete_status == WcStatus::WC_SUCCESS) {
		PfClientIocb *iocb = bd->client_iocb;
		if(bd->data_len == sizeof(PfMessageHead)) {
			if(IS_WRITE_OP(bd->cmd_bd->opcode)) {
				//message head sent complete
				conn->add_ref(); //for start send data
				IoSubTask* t = (IoSubTask*)iocb->ulp_arg;
				conn->start_send(t->parent_iocb->data_bd);
				return 1;
			}

		} else if(bd->data_len == sizeof(PfMessageReply)) {
			struct PfMessageReply *reply = bd->reply_bd;
			iocb = conn->replicator->pick_iocb(reply->command_id, reply->command_seq);
			if (unlikely(iocb == NULL))
			{
				S5LOG_WARN("Previous replicating IO back but timeout!");
				conn->replicator->mem_pool.reply_pool.free(bd);
				return 0;
			}
//			if(iocb->reply_bd != bd) {
//				bd->client_iocb->reply_bd = iocb->reply_bd;
//				iocb->reply_bd = bd;
//			}
			iocb->reply_bd = bd;
			if(IS_READ_OP(iocb->cmd_bd->cmd_bd->opcode)) {
				conn->add_ref(); //for start receive data
				//S5LOG_DEBUG("replicator receive reply ok, to read data %d bytes", iocb->data_bd->data_len);
				conn->start_recv(iocb->data_bd);
				return 1;
			}
			conn->put_throttle();
			//receive reply means IO completed for write
			return conn->replicator->event_queue->post_event(EVT_IO_COMPLETE, 0, iocb);
		} else if(IS_READ_OP(iocb->cmd_bd->cmd_bd->opcode)) { //complete of receive data payload
			conn->put_throttle();
			return conn->replicator->event_queue->post_event(EVT_IO_COMPLETE, 0, iocb);
		}
		//for other status, like data write completion, lets continue wait for reply receive
		return 0;
	} else if(unlikely(complete_status == WcStatus::WC_FLUSH_ERR)) {
		//for unclean close, IO will be resend in `handle_conn_close`
		conn->unclean_closed = true;



		/**
		 * for replicator, FLUSH_ERR may happen in
		 *    1) REPLICATE_WRITE   send head
		 *    2)                   send data
		 *    3)                   recv reply
		 *    4) RECOVERY_READ     send head
		 *    5)                   recv data
		 *    6)                   recv reply
		 *
		 *    In any case, IO should be retried on backup connection, for multi-path may exists.
		 *    so, in `replicator_on_conn_close` , it will retry IO
		 */
		S5LOG_ERROR("replicating connection closed, %s", bd->conn->connection_info.c_str());
		if (bd->data_len == sizeof(PfMessageReply)) { //error during receive reply
			S5LOG_WARN("Connection:%p error during receive reply, will resend IO", conn);
			conn->replicator->mem_pool.reply_pool.free(bd);
		} else if(bd->data_len == sizeof(PfMessageHead)) { //error during send head
			auto io = bd->client_iocb;

			//handle_conn_close will resend IO
			S5LOG_WARN("Connection:%p error during send head of:%s, will resend IO", conn, PfOpCode2Str(((SubTask*)io->ulp_arg)->opcode));
//			if(((SubTask*)io->ulp_arg)->opcode == PfOpCode::S5_OP_RECOVERY_READ)
//				((SubTask*)io->ulp_arg)->complete(PfMessageStatus::MSG_STATUS_CONN_LOST);
//			else
//				app_context.error_handler->submit_error((IoSubTask*)io->ulp_arg, PfMessageStatus::MSG_STATUS_CONN_LOST);
//			io->data_bd = NULL;
//			conn->replicator->iocb_pool.free(io);
		} else { //error during send/recv data
			//since data_bd is the same bd from PfServerIocb allocated when receiving from client.
			PfClientIocb* io = (PfClientIocb*)bd->cbk_data;
			S5LOG_WARN("Connection:%p error during send/recv data of:%s, will resend IO", conn, PfOpCode2Str(((SubTask*)io->ulp_arg)->opcode));
//			if(((SubTask*)io->ulp_arg)->opcode == PfOpCode::S5_OP_RECOVERY_READ)
//				((SubTask*)io->ulp_arg)->complete(PfMessageStatus::MSG_STATUS_CONN_LOST);
//			else
//				app_context.error_handler->submit_error((IoSubTask*)io->ulp_arg, PfMessageStatus::MSG_STATUS_CONN_LOST);
//
//
//			bellow code should execute only on RECOVERY_READ ?
//			io->data_bd->cbk_data = NULL;
//			io->data_bd = NULL;
//			if(IS_READ_OP(io->cmd_bd->cmd_bd->offset)) {
//				conn->replicator->reply_pool.free(io->reply_bd);
//				io->reply_bd = NULL;
//			}
//
//			conn->replicator->iocb_pool.free(io);
		}
		return 0;
	}
	S5LOG_FATAL("replicator bd complete in unknown status:%d, conn:%s", complete_status, conn->connection_info.c_str());
	return 0;

}
#ifdef WITH_RDMA
static int replicator_on_rdma_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfRdmaConnection* conn = (PfRdmaConnection*)_conn;
	if (complete_status == WcStatus::WC_SUCCESS) {
		if(bd->data_len == sizeof(PfMessageReply))
		{
			PfClientIocb *iocb = bd->client_iocb;
			struct PfMessageReply *reply = bd->reply_bd;
			iocb = conn->replicator->pick_iocb(reply->command_id, reply->command_seq);
			//if (iocb->conn == NULL) {
			//	S5LOG_ERROR("Pick io with no connection, cid:%d", reply->command_id );
			//}
			conn->put_throttle();
			if (unlikely(iocb == NULL))
			{
				S5LOG_WARN("Previous replicating IO back but timeout!");
				conn->replicator->mem_pool.reply_pool.free(bd);
				return 0;
			}
                        // in order to trace reply latency
                        SubTask *t = (SubTask *) iocb->ulp_arg;
                        t->reply_time = spdk_get_ticks();
			return conn->replicator->event_queue->post_event(EVT_IO_COMPLETE, 0, iocb);
		}
		return 0;
	} else  if (unlikely(complete_status == WcStatus::WC_FLUSH_ERR)) {
		conn->unclean_closed = true;
		/**
		 * for replicator, FLUSH_ERR may happen in
		 *    1) REPLICATE_WRITE   send head
		 *    2)                   
		 *    3)                   recv reply
		 *    4) RECOVERY_READ     send head
		 *    5)                   
		 *    6)                   recv reply
		 *
		 *    In any case, IO should be retried on backup connection, for multi-path may exists.
		 *    so, in `replicator_on_conn_close` , it will retry IO
		 */
		S5LOG_ERROR("replicating connection closed, RDMA://%s", bd->conn->connection_info.c_str());
		if (bd->data_len == sizeof(PfMessageReply)) { //error during receive reply
			S5LOG_WARN("Connection:%p error during receive reply, will resend IO", conn);
			conn->replicator->mem_pool.reply_pool.free(bd);
			conn->put_throttle();
		}
		else if (bd->data_len == sizeof(PfMessageHead)) { //error during send head
			auto io = bd->client_iocb;
			S5LOG_WARN("Connection:%p error during send head of:%s, will resend IO", conn, PfOpCode2Str(((SubTask*)io->ulp_arg)->opcode));
			//			if(((SubTask*)io->ulp_arg)->opcode == PfOpCode::S5_OP_RECOVERY_READ)
			//				((SubTask*)io->ulp_arg)->complete(PfMessageStatus::MSG_STATUS_CONN_LOST);
			//			else
			//				app_context.error_handler->submit_error((IoSubTask*)io->ulp_arg, PfMessageStatus::MSG_STATUS_CONN_LOST);
			//			io->data_bd = NULL;
			//			conn->replicator->iocb_pool.free(io);
		}
		else { //error during send/recv data
			//for RDMA, should never reach here, since we a in passive mode for data transfer
			S5LOG_FATAL("Unexcepted state on handle error");
		}
		return 0;
	}
	S5LOG_FATAL("replicator bd complete in unknown status:%d, conn:%s", complete_status, conn->connection_info.c_str());
	return -1;
}
#endif
void replicator_on_conn_close(PfConnection* conn)
{
	if(conn->unclean_closed) {
		conn->add_ref();//keep conn alive during process, dec_ref in handle_conn_close
		//send a event cause Replicator::handle_conn_close call in replicator thread
		conn->replicator->event_queue->post_event(EVT_CONN_CLOSED, 0, conn);
	}
}

int PfReplicator::handle_conn_close(PfConnection *c)
{
	assert(c->unclean_closed);
	for(int i=0;i<iocb_pool.obj_count;i++) {
		PfClientIocb* io = &iocb_pool.data[i];
		if(io->conn == c) {
			PfOpCode op = io->cmd_bd->cmd_bd->opcode;
			S5LOG_WARN("IO:%p %s depends on unclean closed conn:%p, will resend", io, PfOpCode2Str(op), c);

			if(op == PfOpCode::S5_OP_REPLICATE_WRITE){
				//free resource allocated during begin_replicate_io
				IoSubTask* t = (IoSubTask*)io->ulp_arg;
				io->reply_bd = NULL;
				io->data_bd = NULL;
				io->conn = NULL;
				iocb_pool.free(io);
				event_queue->post_event(EVT_IO_REQ, 0, t);
			} else if(op == PfOpCode::S5_OP_RECOVERY_READ) {
				RecoverySubTask* t = (RecoverySubTask*)io->ulp_arg;
				io->reply_bd = NULL;
				io->data_bd = NULL;
				io->conn = NULL;
				iocb_pool.free(io);
				event_queue->post_event(EVT_RECOVERY_READ_IO, 0, t);
			} else if(op == PfOpCode::S5_OP_HEARTBEAT) {
				io->reply_bd = NULL;
				io->data_bd = NULL;
				io->conn = NULL;
				iocb_pool.free(io);
			} else {
				S5LOG_FATAL("Unexpected op:%s in handle_conn_close",  PfOpCode2Str(op));
			}

		}
	}
	c->dec_ref(); //added in replicator_on_conn_close
	return 0;
}

int PfReplicator::init(int index, uint16_t* p_id)
{
	int rc;
	rep_index = index;
	snprintf(name, sizeof(name), "%d_replicator", rep_index);
	int rep_iodepth = PF_MAX_IO_DEPTH; //io depth in single connection
	int rep_iocb_depth = 8192; //total IO's in processing
	PfEventThread::init(name, rep_iocb_depth, *p_id);
	*p_id=(*p_id)+1;
	Cleaner clean;
	tcp_poller = new PfPoller();
	if(tcp_poller == NULL) {
		S5LOG_ERROR("No memory to alloc replicator poller:%d", index);
		return -ENOMEM;
	}
	clean.push_back([this](){delete tcp_poller; tcp_poller = NULL;});
	//TODO: max_fd_count in poller->init should depend on how many server this volume layed on
	rc = tcp_poller->init(format_string("reppoll_%d", index).c_str(), 128);
	if(rc != 0) {
		S5LOG_ERROR("tcp_poller init failed, rc:%d", rc);
		return rc;
	}
	conn_pool = new PfRepConnectionPool();
	if (conn_pool == NULL) {
		S5LOG_ERROR("No memory to alloc connection pool");
		return -ENOMEM;
	}
	clean.push_back([this]{delete conn_pool; conn_pool=NULL;});
	if(rep_conn_type == TCP_TYPE) {
		//this if for tcp connection
		conn_pool->init(128, tcp_poller, this, 0, rep_iodepth, TCP_TYPE, replicator_on_tcp_network_done, replicator_on_conn_close);
	} else {
		//this if for rdma connection
#ifdef WITH_RDMA
		conn_pool->init(128, tcp_poller, this, 0, rep_iodepth, RDMA_TYPE, replicator_on_rdma_network_done, replicator_on_conn_close);
#else
		S5LOG_FATAL("RDMA not enabled, please compile with flag -DWITH_RDMA=1");
#endif
	}

	int iocb_pool_size = rep_iocb_depth;
	rc = mem_pool.cmd_pool.init(sizeof(PfMessageHead), iocb_pool_size);
	if(rc != 0){
		S5LOG_ERROR("Failed to init cmd_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this](){mem_pool.cmd_pool.destroy();});

	rc = mem_pool.reply_pool.init(sizeof(PfMessageReply), iocb_pool_size);
	if(rc != 0){
		S5LOG_ERROR("Failed to init reply_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this](){mem_pool.reply_pool.destroy();});

	rc = iocb_pool.init(iocb_pool_size);
	if(rc != 0){
		S5LOG_ERROR("Failed to init iocb_pool, rc:%d", rc);
		return rc;
	}
	for(int i=0;i< iocb_pool_size;i++)
	{
		PfClientIocb* io = iocb_pool.alloc();
		io->cmd_bd = mem_pool.cmd_pool.alloc();
		io->cmd_bd->cmd_bd->command_id = (uint16_t )i;
		io->cmd_bd->data_len = io->cmd_bd->buf_capacity;
		io->cmd_bd->client_iocb = io;
		io->data_bd = NULL;
		io->reply_bd = NULL;
		BufferDescriptor* rbd = mem_pool.reply_pool.alloc();
		rbd->data_len = rbd->buf_capacity;
		rbd->client_iocb = NULL;
		mem_pool.reply_pool.free(rbd);
		iocb_pool.free(io);
	}
#ifdef USE_DELAY_THREAD
	delay_thread.replicator = this;
	char delay_name[32];
        snprintf(delay_name, sizeof(delay_name), "%d_delay", rep_index);
	delay_thread.init(delay_name, rep_iocb_depth, *p_id);
	delay_thread.start();
#endif
	clean.cancel_all();
	return 0;
}