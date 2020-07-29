#include <unistd.h>
#include <pf_tcp_connection.h>

#include "pf_dispatcher.h"
#include "pf_replicator.h"
#include "pf_client_priv.h"
#include "pf_message.h"
#include "pf_main.h"


int PfReplicator::replicate_io(IoSubTask* t)
{
	int rc;
	PfClientIocb* io = iocb_pool.alloc();
	if (unlikely(io == NULL)) {
		S5LOG_ERROR("Failed to allock IOCB for replicating");
		usleep(100);
		event_queue.post_event(EVT_IO_REQ, 0, t); //requeue the request
		return -EAGAIN;
	}

	auto old_cid = io->cmd_bd->cmd_bd->command_id;
	memcpy(io->cmd_bd->cmd_bd, t->parent_iocb->cmd_bd->cmd_bd, sizeof(PfMessageHead));
	io->cmd_bd->cmd_bd->command_id = old_cid;
	assert(io->cmd_bd->cmd_bd->opcode == PfOpCode::S5_OP_WRITE);
	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;

	io->user_buf = NULL;
	io->data_bd = t->parent_iocb->data_bd;
	io->data_bd->cbk_data = io;
	io->ulp_arg = t;
	cmd->opcode = PfOpCode::S5_OP_REPLICATE_WRITE;
	cmd->buf_addr = (__le64) io->data_bd->buf;

	PfConnection* c = conn_pool->get_conn((int)t->rep->store_id);
	if(c == NULL) {
		S5LOG_ERROR("Failed get connection to store:%d", t->rep->store_id);
		app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_CONN_LOST);
		return PfMessageStatus::MSG_STATUS_CONN_LOST;
	}
	BufferDescriptor* rbd = reply_pool.alloc();
	if(unlikely(rbd == NULL))
	{
		S5LOG_ERROR("replicator[%d] has no recv_bd availableio now, requeue IO", rep_index);
		usleep(100);
		event_queue.post_event(EVT_IO_REQ, 0, t); //requeue the request
		iocb_pool.free(io);
		return -EAGAIN;
	}
	rc = c->post_recv(rbd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed to post_recv in replicator[%d], connection:%s, rc:%d", rep_index, c->connection_info.c_str(), rc);
		usleep(100);
		event_queue.post_event(EVT_IO_REQ, 0, t); //requeue the request
		reply_pool.free(rbd);
		iocb_pool.free(io);
		return -EAGAIN;
	}
	rc = c->post_send(io->cmd_bd);
	if(unlikely(rc)) {
		S5LOG_ERROR("Failed to post_send in replicator[%d], connection:%s, rc:%d", rep_index, c->connection_info.c_str(), rc);
		usleep(100);
		event_queue.post_event(EVT_IO_REQ, 0, t); //requeue the request
		//cann't free reply bd, since it was post into connection
		iocb_pool.free(io);
		return -EAGAIN;
	}
	return rc;
}

int PfReplicator::process_io_complete(BufferDescriptor* bd, int complete_status)
{
	if(unlikely(complete_status == WcStatus::TCP_WC_FLUSH_ERR))
	{
		S5LOG_ERROR("replicating connection closed, %s,  requeue IO", bd->conn->connection_info.c_str());
		if(bd->data_len == sizeof(PfMessageReply)) { //error during receive reply
			reply_pool.free(bd);
		} else if(bd->data_len == sizeof(PfMessageHead)) { //error during send head
			auto io = bd->client_iocb;
			app_context.error_handler->submit_error((IoSubTask*)io->ulp_arg, PfMessageStatus::MSG_STATUS_CONN_LOST);
			io->data_bd = NULL;
			iocb_pool.free(io);
		} else { //error during send data
			//since data_bd is the same bd from PfServerIocb allocated when receiving from client.
			PfClientIocb* io = (PfClientIocb*)bd->cbk_data;
			app_context.error_handler->submit_error((IoSubTask*)io->ulp_arg, PfMessageStatus::MSG_STATUS_CONN_LOST);
			io->data_bd->cbk_data = NULL;
			io->data_bd = NULL;
			iocb_pool.free(io);
		}
		return 0;
	}
	assert(complete_status == WcStatus::TCP_WC_SUCCESS);
	if(bd->data_len == sizeof(PfMessageReply)) { //error during receive reply
		PfConnection* conn = bd->conn;

		struct PfMessageReply *reply = bd->reply_bd;
		PfClientIocb* io = pick_iocb(reply->command_id, reply->command_seq);
		io->reply_bd = bd;
		PfMessageHead* io_cmd = io->cmd_bd->cmd_bd;
		uint64_t ms1 = 1000;
		/*
		 * In io timeout case, we just ignore this completion
		 */
		if (unlikely(io == NULL))
		{
			S5LOG_WARN("Previous replicating IO back but timeout!");
			reply_pool.free(bd);
			return 0;
		}
		io->reply_time = now_time_usec();
		uint64_t io_elapse_time = (io->reply_time - io->submit_time) / ms1;
		if (unlikely(io_elapse_time > 2000))
		{
			S5LOG_WARN("SLOW IO, shard id:%d, command_id:%d, op:%s, since submit:%dms since send:%dms",
					   io_cmd->offset >> SHARD_SIZE_ORDER,
					   io_cmd->command_id,
					   PfOpCode2Str(io_cmd->opcode),
					   io_elapse_time,
					   (io->reply_time-io->sent_time)/ms1
			);
		}
		PfMessageStatus s = (PfMessageStatus)reply->status;

		//On client side, we rely on the io timeout mechnism to release time connection
		//Here we just release the io task
		if (unlikely(io_cmd->opcode == S5_OP_HEARTBEAT))
		{
			__sync_fetch_and_sub(&conn->inflying_heartbeat, 1);
			reply_pool.free(io->reply_bd);
			io->reply_bd = NULL;
			iocb_pool.free(io);
			return 0;
		}

		IoSubTask* t = (IoSubTask*)io->ulp_arg;
		t->complete(s);
		reply_pool.free(io->reply_bd);
		io->reply_bd = NULL;
		io->data_bd = NULL;
		iocb_pool.free(io);
	} else if(bd->data_len == sizeof(PfMessageHead)) {

	} else {

	}
	return 0;
}
int PfReplicator::process_event(int event_type, int arg_i, void *arg_p)
{
	switch(event_type) {
		case EVT_IO_REQ:
			return replicate_io((IoSubTask*)arg_p);
			break;
		case EVT_IO_COMPLETE:
			return process_io_complete((BufferDescriptor*)arg_p, arg_i);
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
	if(pos == peers.end())
		return NULL;
	PeerAddr& addr = pos->second;
	if(addr.conn != NULL && addr.conn->state == CONN_OK)
		return addr.conn;
	for(int i=0;i<addr.ip.size();i++){
		addr.conn = PfConnectionPool::get_conn(addr.ip[addr.curr_ip_idx]);
		if(addr.conn)
			return addr.conn;
		addr.curr_ip_idx = (addr.curr_ip_idx+1)%(int)addr.ip.size();
	}
	return NULL;
}

static int replicator_on_tcp_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfTcpConnection* conn = (PfTcpConnection*)_conn;
	if(complete_status == WcStatus::TCP_WC_SUCCESS) {

		if(bd->data_len == sizeof(PfMessageHead)) {
			if(bd->cmd_bd->opcode == PfOpCode::S5_OP_REPLICATE_WRITE) {
				//message head sent complete
				PfClientIocb *iocb = bd->client_iocb;
				conn->add_ref(); //for start send data
				IoSubTask* t = (IoSubTask*)iocb->ulp_arg;
				conn->start_send(t->parent_iocb->data_bd); //on client side, use use's buffer
				return 1;
			}
			assert(bd->cmd_bd->opcode == PfOpCode::S5_OP_HEARTBEAT);
		}
	}

	return conn->replicator->event_queue.post_event(EVT_IO_COMPLETE, complete_status, bd);
}

int PfReplicator::init(int index)
{
	int rc;
	rep_index = index;
	snprintf(name, sizeof(name), "%d_replicator", rep_index);
	int rep_iodepth = 64;
	PfEventThread::init(name, rep_iodepth*2);
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
	conn_pool->init(128, tcp_poller, this, 0, rep_iodepth, replicator_on_tcp_network_done);

	rc = cmd_pool.init(sizeof(PfMessageHead), rep_iodepth);
	if(rc != 0){
		S5LOG_ERROR("Failed to init cmd_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this](){cmd_pool.destroy();});

	rc = reply_pool.init(sizeof(PfMessageReply), rep_iodepth);
	if(rc != 0){
		S5LOG_ERROR("Failed to init reply_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this](){reply_pool.destroy();});

	rc = iocb_pool.init(rep_iodepth);
	if(rc != 0){
		S5LOG_ERROR("Failed to init iocb_pool, rc:%d", rc);
		return rc;
	}
	for(int i=0;i<rep_iodepth;i++)
	{
		PfClientIocb* io = iocb_pool.alloc();
		io->cmd_bd = cmd_pool.alloc();
		io->cmd_bd->cmd_bd->command_id = (uint16_t )i;
		io->cmd_bd->data_len = io->cmd_bd->buf_capacity;
		io->cmd_bd->client_iocb = io;
		io->data_bd = NULL;
		io->reply_bd = NULL;
		BufferDescriptor* rbd = reply_pool.alloc();
		rbd->data_len = rbd->buf_capacity;
		rbd->client_iocb = NULL;
		reply_pool.free(rbd);
		iocb_pool.free(io);
	}
	clean.cancel_all();
	return 0;
}