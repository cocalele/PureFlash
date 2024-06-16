#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <zookeeper.h>
#include <curl/curl.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <string>
#include <vector>
#include <mutex>
#include <future>

#include "pf_connection_pool.h"
#include "pf_connection.h"
#include "pf_client_priv.h"
#include <nlohmann/json.hpp>
#include <pf_tcp_connection.h>
#include "pf_message.h"
#include "pf_poller.h"
#include "pf_buffer.h"
#include "pf_client_api.h"
#include "pf_app_ctx.h"
#include "pf_client_store.h"

using namespace std;
using nlohmann::json;
size_t iov_from_buf(const struct iovec *iov, unsigned int iov_cnt, const void *buf, size_t bytes);

static const char* pf_lib_ver = "S5 client version:0x00010000";

enum connection_type client_conn_type = RDMA_TYPE;

#define CLIENT_TIMEOUT_CHECK_INTERVAL 1 //seconds
#define RPC_TIMEOUT_SEC 2

const char* default_cfg_file = "/etc/pureflash/pf.conf";
void from_json(const json& j, GeneralReply& reply)
{
	j.at("op").get_to(reply.op);
	j.at("ret_code").get_to(reply.ret_code);
	if (reply.ret_code != 0) {
		if (j.contains("reason"))
			j.at("reason").get_to(reply.reason);
	}
}

void from_json(const json& j, PfClientShardInfo& p) {
	j.at("index").get_to(p.index);
	j.at("store_ips").get_to(p.store_ips);
	j.at("status").get_to(p.status);

}
void from_json(const json& j, PfClientVolume& p) {
	j.at("status").get_to(p.status);
	j.at("volume_name").get_to(p.volume_name);
	j.at("volume_size").get_to(p.volume_size);
	j.at("volume_id").get_to(p.volume_id);
	j.at("shard_count").get_to(p.shard_count);
	j.at("rep_count").get_to(p.rep_count);
	j.at("meta_ver").get_to(p.meta_ver);
	j.at("snap_seq").get_to(p.snap_seq);
	j.at("shards").get_to(p.shards);
	j.at("shard_lba_cnt_order").get_to(p.shard_lba_cnt_order);
}
void from_json(const json& j, PfClientVolumeInfo& p) {
	string temp;
	j.at("status").get_to(temp);
	strcpy(p.status, temp.c_str());
	j.at("name").get_to(temp);
	strcpy(p.volume_name, temp.c_str());
	j.at("size").get_to(p.volume_size);
	j.at("id").get_to(p.volume_id);
	j.at("rep_count").get_to(p.rep_count);
	j.at("meta_ver").get_to(p.meta_ver);
	j.at("snap_seq").get_to(p.snap_seq);
}
void from_json(const json&j, ListVolumeReply &reply)
{
	j.at("volumes").get_to(reply.volumes);
	j.at("ret_code").get_to(reply.ret_code);
	if(reply.ret_code != 0)
		j.at("reason").get_to(reply.reason);
}

#define MAX_URL_LEN 2048

#define VOLUME_MONITOR_INTERVAL 30 //seconds

#define ZK_TIMEOUT 5000


/**
 * Thread model in client
 * 1. User thread(s), user thread will call qfa_open_volume, qfa_aio
 * 2. volume_proc thread, the major work thread to process volume works
 * 3. rdma_cm thread, handle connection establish and close
 * 4. rdma completion thread, wait on rdam completion channel and call rdma completion
 * 5. timeout check thread
 */
typedef struct curl_memory {
	char *memory;
	size_t size;
}curl_memory_t;


struct PfClientVolume* _pf_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,
                                      int lib_ver, bool is_aof)
{
	int rc = 0;
	if (lib_ver != S5_LIB_VER)
	{
		S5LOG_ERROR("Caller lib version:%d mismatch lib:%d", lib_ver, S5_LIB_VER);
		return NULL;
	}
	S5LOG_INFO("Opening volume %s@%s", volume_name,
		(snap_name == NULL || strlen(snap_name) == 0) ? "HEAD" : snap_name);
	try
	{
		Cleaner _clean;
		PfClientVolume* volume = new PfClientVolume;
		if (volume == NULL)
		{
			S5LOG_ERROR("alloca memory for volume failed!");
			return NULL;
		}
		_clean.push_back([volume]() { delete volume; });
		//other calls
		volume->volume_name = volume_name;
		if(cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		volume->cfg_file = cfg_filename;
		if(snap_name)
			volume->snap_name = snap_name;

		rc = volume->do_open(false, is_aof);
		if (rc)	{
			return NULL;
		}
		volume->runtime_ctx->add_volume(volume);

		S5LOG_INFO("Succeeded open volume %s@%s(0x%lx), meta_ver=%d, io_depth=%d", volume->volume_name.c_str(),
			volume->snap_seq == -1 ? "HEAD" : volume->snap_name.c_str(), volume->volume_id, volume->meta_ver, volume->io_depth);

		_clean.cancel_all();
		return volume;
	}
	catch (std::exception& e)
	{
		S5LOG_ERROR("Exception in open volume:%s", e.what());
	}
	return NULL;
}

struct PfClientVolume* pf_half_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,
	int lib_ver)
{
	S5LOG_INFO("Half open volume:%s", volume_name);
	try	{
		Cleaner _clean;
		PfClientVolume* volume = new PfClientVolume;
		if (volume == NULL)
		{
			S5LOG_ERROR("alloca memory for volume failed!");
			return NULL;
		}
		_clean.push_back([volume]() { delete volume; });
		//other calls
		volume->volume_name = volume_name;
		if (cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		volume->cfg_file = cfg_filename;
		if (snap_name)
			volume->snap_name = snap_name;

		int rc = 0;
		conf_file_t cfg = conf_open(cfg_filename);
		if (cfg == NULL)
		{
			S5LOG_ERROR("Failed open config file:%s", cfg_filename);
			return NULL;
		}
		DeferCall _cfg_r([cfg]() { conf_close(cfg); });

		char* esc_vol_name = curl_easy_escape(NULL, volume_name, 0);
		if (!esc_vol_name)
		{
			S5LOG_ERROR("Curl easy escape failed.");
			return NULL;
		}
		DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });
		char* esc_snap_name = curl_easy_escape(NULL, snap_name, 0);
		if (!esc_snap_name)
		{
			S5LOG_ERROR("Curl easy escape failed.");
			return NULL;
		}
		DeferCall _2([esc_snap_name]() { curl_free(esc_snap_name); });
		const char* op = "open_volume";
		std::string query = format_string("op=%s&volume_name=%s&snapshot_name=%s", op, esc_vol_name, esc_snap_name);
		rc = query_conductor(cfg, query, *volume);
		if (rc != 0) {
			S5LOG_ERROR("Failed query conductor, rc:%d", rc);
			return NULL;
		}
		for (int i = 0; i < volume->shards.size(); i++) {
			volume->shards[i].parsed_store_ips = split_string(volume->shards[i].store_ips, ',');
		}
		volume->state = VOLUME_OPENED;
		S5LOG_INFO("Succeeded open volume %s@%s(0x%lx), meta_ver=%d, io_depth=%d", volume->volume_name.c_str(),
			volume->snap_seq == -1 ? "HEAD" : volume->snap_name.c_str(), volume->volume_id, volume->meta_ver, volume->io_depth);

		_clean.cancel_all();
		return volume;
	}catch (std::exception& e)	{
		S5LOG_ERROR("Exception in open volume:%s", e.what());
	}
	return NULL;
}
struct PfClientVolume* pf_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,	int lib_ver)
{
	return _pf_open_volume(volume_name, cfg_filename, snap_name, lib_ver, false);
}

int pf_query_volume_info(const char* volume_name, const char* cfg_filename, const char* snap_name,
                                       int lib_ver, struct PfClientVolumeInfo* volume)
{
	int rc = 0;
	if (lib_ver != S5_LIB_VER)
	{
		S5LOG_ERROR("Caller lib version:%d mismatch lib:%d", lib_ver, S5_LIB_VER);
		return -EINVAL;
	}
	S5LOG_INFO("Opening volume %s@%s", volume_name,
	           (snap_name == NULL || strlen(snap_name) == 0) ? "HEAD" : snap_name);
	try
	{
		Cleaner _clean;
		ListVolumeReply reply;

		if(cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		conf_file_t cfg = conf_open(cfg_filename);
		if(cfg == NULL)
		{
			S5LOG_ERROR("Failed open config file:%s, rc:%d", cfg_filename, -errno);
			return -errno;
		}
		DeferCall _cfg_r([cfg]() { conf_close(cfg); });

		char* esc_vol_name = curl_easy_escape(NULL, volume_name, 0);
		if (!esc_vol_name)
		{
			S5LOG_ERROR("Curl easy escape failed. ENOMEM");
			return -ENOMEM;
		}
		DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });
		std::string query = format_string("op=list_volume&name=%s", esc_vol_name);
		char* esc_snap_name = NULL;
		if(snap_name != NULL) {
			curl_easy_escape(NULL, snap_name, 0);
			if (!esc_snap_name) {
				S5LOG_ERROR("Curl easy escape failed. ENOMEM");
				return -ENOMEM;
			}
			query += format_string("&snap_name=%s", esc_snap_name);
			curl_free(esc_snap_name);
		}

		rc = query_conductor(cfg, query, reply);
		if (rc != 0)
		{
			S5LOG_ERROR("Failed query conductor, rc:%d", rc);
			return rc;
		}

		if(reply.volumes.size() < 1) {
			S5LOG_ERROR("Volume:%s not exists or can't be open", volume_name);
			return -ENOENT;
		}
		*volume = reply.volumes[0];
		S5LOG_INFO("Succeeded query volume %s@%s(0x%lx), meta_ver=%d", volume->volume_name,
		           snap_name == NULL ? "HEAD" : volume->snap_name, volume->volume_id, volume->meta_ver);

		_clean.cancel_all();
		return 0;
	}
	catch (std::exception& e)
	{
		S5LOG_ERROR("Exception in open volume:%s", e.what());
	}
	return -1;
}

// should be run in volume proc
static int client_on_tcp_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfTcpConnection* conn = (PfTcpConnection*)_conn;
	if (unlikely(complete_status != WC_SUCCESS))
	{
		S5LOG_INFO("Op complete unsuccessful opcode:%d, status:%s", bd->wr_op, WcStatusToStr(WcStatus(complete_status)));
		if(bd->wr_op == TCP_WR_SEND){
			//IO is resend in function `client_on_tcp_close`
			return 0;
		} else if(bd->wr_op == TCP_WR_RECV){
			PfConnection* conn = bd->conn;
			conn->client_ctx->reply_pool.free(bd);
			//conn->close(); //connection should has been closed during do_send/do_recv
			//IO not completed, resend in function `client_on_tcp_close`
			return 0;

		}
	}
	//S5LOG_INFO("Op complete opcode:%d, status:%s, len:%d", bd->wr_op, WcStatusToStr(WcStatus(complete_status)), bd->data_len);

	if(complete_status == WcStatus::WC_SUCCESS) {

		if(bd->data_len == sizeof(PfMessageHead) ) {
			if(bd->cmd_bd->opcode == PfOpCode::S5_OP_WRITE) {
				//message head sent complete, continue to send data for write OP
				PfClientIocb* iocb = bd->client_iocb;
				conn->add_ref(); //for start send data
				iocb->data_bd->conn = conn;
				conn->start_send(iocb->data_bd); //on client side, use user's buffer
			}
			return 1;
		} else if(bd->data_len == sizeof(PfMessageReply) ) {
			//assert(bd->reply_bd->command_id<32);
			PfClientIocb* iocb = conn->client_ctx->pick_iocb(bd->reply_bd->command_id, bd->reply_bd->command_seq);
			//In io timeout case, we just ignore this completion

			if (unlikely(iocb == NULL)) {
				//this should never happen, since whenever an IO timeout, the connection will be closed
				S5LOG_FATAL("Previous IO cid:%d back but timeout!", bd->reply_bd->command_id);
				return 1;
			}

			iocb->reply_bd = bd;
			bd->client_iocb = iocb;
			if(iocb != NULL && iocb->cmd_bd->cmd_bd->opcode == PfOpCode::S5_OP_READ) {
				conn->add_ref(); //for start recv data
				iocb->data_bd->conn = conn;
				//S5LOG_DEBUG("To recv %d bytes data payload", iocb->data_bd->data_len);
				conn->start_recv(iocb->data_bd); //on client side, use use's buffer
				return 1;
			}
			return iocb->volume->event_queue->post_event(EVT_IO_COMPLETE, complete_status, bd, iocb->volume);
		} else  {//this is for data receive or data send complete
			// this happens in condition
			// b) data send complete, for WRITE
			// c) data receive complete, for READ

			PfClientIocb* iocb = bd->client_iocb;
			if(iocb->cmd_bd->cmd_bd->opcode == PfOpCode::S5_OP_READ){ //case c
				//for READ, need to complete IO
				PfClientIocb* iocb = bd->client_iocb;
				return iocb->volume->event_queue->post_event(EVT_IO_COMPLETE, complete_status, bd, iocb->volume);

			}
			return 0;
		}
	}
	S5LOG_ERROR("Unexpected status:%d, data_len=%d", complete_status, bd->data_len);
	return -1;
}

static int client_on_rdma_network_done(BufferDescriptor* bd, WcStatus complete_status, PfConnection* _conn, void* cbk_data)
{
	PfRdmaConnection* conn = (PfRdmaConnection*)_conn;
	if(unlikely(complete_status == WC_FLUSH_ERR)){
		if (bd->wr_op == RDMA_WR_RECV) {
			conn->client_ctx->reply_pool.free(bd);
		}
		//we don't handle wr_op type SEND, which is for IO command request. timeout mechanism will reuse it
		//also, ref_count is decreased during handle EVT_IO_TIMEOUT
	} else {
		if (bd->wr_op == RDMA_WR_RECV) {
			PfClientIocb* iocb = conn->client_ctx->pick_iocb(bd->reply_bd->command_id, bd->reply_bd->command_seq);
			iocb->reply_bd = bd;
			bd->client_iocb = iocb;
			iocb->volume->event_queue->post_event(EVT_IO_COMPLETE, complete_status, bd, iocb->volume);
		}else {
			//S5LOG_INFO("get opcode:%d", bd->wr_op);
			//do nothing
		}
	}

	return 0;
}

static void client_on_rdma_close(PfConnection* c)
{

}

static void client_on_tcp_close(PfConnection* _c)
{
	//c->dec_ref(); //Don't dec_ref here, only dec_ref when connection removed from pool
	PfTcpConnection* c = (PfTcpConnection*)_c;
	ObjectMemoryPool<PfClientIocb> *iocb_pool = &c->client_ctx->iocb_pool;
	int cnt = 0;
	struct PfClientIocb* ios = iocb_pool->data;
	
	for (int i = 0; i < iocb_pool->obj_count; i++)
	{
		if (ios[i].conn == c && (ios[i].volume->state == VOLUME_OPENED || ios[i].volume->state == VOLUME_WAIT_REOPEN))
		{
			ios[i].conn->dec_ref();
			ios[i].conn = NULL;
			ios[i].volume->resend_io(&ios[i]);
			cnt++;
		}
	}
	if(cnt > 0){
		S5LOG_WARN("%d IO resent for connection:%p closed", cnt, c);
	}
}
static inline PfClientAppCtx* get_client_ctx()
{
	((PfClientAppCtx*)g_app_ctx)->add_ref();
	return (PfClientAppCtx * )g_app_ctx;
}
static int init_app_ctx(conf_file_t cfg, int io_depth, int max_vol_cnt, int io_timeout)
{
	static int inited = 0;
	static std::mutex _m;
	const std::lock_guard<std::mutex> lock(_m);
	if (inited)
		return 0;
	int rc = 0;
	Cleaner clean;
	PfClientAppCtx* ctx = new PfClientAppCtx();
	assert(ctx->ref_count == 1);
	clean.push_back([ctx]() { ctx->dec_ref(); });
	S5LOG_INFO("Init global app context, iodepth=%d max_vol_cnt=%d", io_depth, max_vol_cnt);
	rc = ctx->init(cfg, io_depth, max_vol_cnt, 0 /* 0 for shared connection*/, io_timeout);
	if(rc != 0){
		S5LOG_ERROR("Failed to init global app context");
		return rc;
	}
	clean.cancel_all();
	g_app_ctx = ctx;
	g_app_ctx->rdma_client_only = true;
	inited = 1;
	return rc;
}


static void client_complete(SubTask* t, PfMessageStatus comp_status) {
	//this function called from SSD IO polling thread
	t->complete_status = comp_status;
	//client_iocb->ulp_handler(client_iocb->ulp_arg, comp_status);
	//that's what do in PfDispatcher::dispatch_complete on server side

	PfClientIocb* parent_iocb = ((PfClientIocb*)t->parent_iocb);
	parent_iocb->task_mask &= (~t->task_mask);


	parent_iocb->ulp_handler(parent_iocb->ulp_arg, comp_status);
	parent_iocb->volume->runtime_ctx->free_iocb(parent_iocb);
}
static void client_complete(SubTask* t, PfMessageStatus comp_status, uint16_t meta_ver) {
	assert(0);
}
static TaskCompleteOps _client_task_complete_ops = { client_complete, client_complete };


int PfClientAppCtx::init(conf_file_t cfg, int io_depth, int max_vol_cnt, uint64_t vol_id, int io_timeout)
{
	if (io_depth == 0)
		return 0;
	Cleaner clean;
	int rc = 0;
	this->io_timeout = io_timeout;
	sem_init(&io_throttle, 0, io_depth);
	const char* zk_ip = conf_get(cfg, "zookeeper", "ip", "", TRUE);
	if (zk_ip == NULL)
	{
		throw std::runtime_error("zookeeper ip not found in conf file");
	}
	const char* cluster_name = conf_get(cfg, "cluster", "name", "cluster1", FALSE);
	rc = zk_client.init(zk_ip, ZK_TIMEOUT, cluster_name); //will be destroyed in destructor
	
	tcp_poller = new PfPoller();
	if (tcp_poller == NULL) {
		S5LOG_ERROR("No memory to alloc poller");
		return -ENOMEM;
	}
	clean.push_back([this]() {delete this->tcp_poller; });
	//TODO: max_fd_count in poller->init should depend on how many server this volume layed on
	// tcp poller is unnecessary for rdma client
	rc = tcp_poller->init("pf_client", 256);
	if (rc != 0) {
		S5LOG_ERROR("tcp_poller init failed, rc:%d", rc);
		return rc;
	}
	conn_pool = new PfConnectionPool();
	if (conn_pool == NULL) {
		S5LOG_ERROR("No memory to alloc connection pool");
		return -ENOMEM;
	}
	clean.push_back([this] {delete conn_pool;  });
	if (client_conn_type == TCP_TYPE) {
		rc = conn_pool->init(256, tcp_poller, this, vol_id ,
			io_depth, TCP_TYPE,	client_on_tcp_network_done, client_on_tcp_close);
	}else{
		rc = conn_pool->init(256, tcp_poller, this, vol_id,
			io_depth, RDMA_TYPE, client_on_rdma_network_done, client_on_rdma_close);
	}
	if (rc != 0) {
		S5LOG_ERROR("conn_pool init failed, rc:%d", rc);
		return rc;
	}
	vol_proc = new PfVolumeEventProc(this);
	if(vol_proc == NULL){
		S5LOG_ERROR("No memory to alloc PfVolumeEventProc");
		return -ENOMEM;
	}
	clean.push_back([this] {delete this->vol_proc;  });
	int poller_id = 0;
	rc = vol_proc->init("vol_proc", io_depth* max_vol_cnt * 4, poller_id);
	if (rc != 0) {
		S5LOG_ERROR("vol_proc init failed, rc:%d", rc);
		return rc;
	}

	int resource_cnt = io_depth * 2;
	rc = data_pool.init(PF_MAX_IO_SIZE, resource_cnt);
	if (rc != 0) {
		S5LOG_ERROR("Failed to init data_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this]() {data_pool.destroy(); });

	rc = cmd_pool.init(sizeof(PfMessageHead), resource_cnt);
	if (rc != 0) {
		S5LOG_ERROR("Failed to init cmd_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this]() {cmd_pool.destroy(); });

	rc = reply_pool.init(sizeof(PfMessageReply), resource_cnt);
	if (rc != 0) {
		S5LOG_ERROR("Failed to init reply_pool, rc:%d", rc);
		return rc;
	}
	clean.push_back([this]() {reply_pool.destroy(); });
	mr_registered = false;

	rc = iocb_pool.init(resource_cnt);
	if (rc != 0) {
		S5LOG_ERROR("Failed to init iocb_pool, rc:%d", rc);
		return rc;
	}
	rc = vol_proc->start();
	if (rc != 0) {
		S5LOG_ERROR("vol_proc start failed, rc:%d", rc);
		return rc;
	}
	for (int i = 0; i < resource_cnt; i++)
	{
		PfClientIocb* io = iocb_pool.alloc();
		io->cmd_bd = cmd_pool.alloc();
		io->cmd_bd->cmd_bd->command_id = (uint16_t)i;
		io->cmd_bd->data_len = io->cmd_bd->buf_capacity;
		io->cmd_bd->client_iocb = io;
		io->data_bd = data_pool.alloc();
		io->data_bd->client_iocb = io;
		io->reply_bd = NULL;
		BufferDescriptor* rbd = reply_pool.alloc();
		rbd->data_len = rbd->buf_capacity;
		rbd->client_iocb = NULL;
		for (int i = 0; i < 1; i++) {//code copied from pf_dispatcher, keep for loop for consistency
			io->subtasks[i] = &io->io_subtasks[i];
			io->subtasks[i]->rep_index = i;
			io->subtasks[i]->task_mask = 1 << i;
			io->subtasks[i]->parent_iocb = io;
			io->subtasks[i]->ops = &_client_task_complete_ops;
		}
		io->task_mask = 0;
		reply_pool.free(rbd);
		iocb_pool.free(io);
	}

	timeout_thread = std::thread([this]() {
		timeout_check_proc();
	});
	clean.cancel_all();
	return 0;
}
int PfClientVolume::do_open(bool reopen, bool is_aof)
{
	int rc = 0;
	conf_file_t cfg = conf_open(cfg_file.c_str());
	if(cfg == NULL)
	{
		S5LOG_ERROR("Failed open config file:%s", cfg_file.c_str());
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });
	io_depth = conf_get_int(cfg, "client", "io_depth", 120, FALSE);
	// server limit io_depth to 255, client must half of that
	if (io_depth > PF_MAX_IO_DEPTH) {
		S5LOG_ERROR("io_depth:%d exceed max allowed:%d", io_depth, PF_MAX_IO_DEPTH);
		return -EINVAL;
	}
	int io_timeout = conf_get_int(cfg, "client", "io_timeout", 30, FALSE);
	const char *conn_type = conf_get(cfg, "client", "conn_type", "rdma", FALSE);
	if (strcmp(conn_type, "rdma") == 0)
		client_conn_type = RDMA_TYPE;
	else
		client_conn_type = TCP_TYPE;

	char* esc_vol_name = curl_easy_escape(NULL, volume_name.c_str(), 0);
	if (!esc_vol_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });
	char* esc_snap_name = curl_easy_escape(NULL, snap_name.c_str(), 0);
	if (!esc_snap_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _2([esc_snap_name]() { curl_free(esc_snap_name); });
	const char* op = is_aof ? "open_aof" : "open_volume";
	std::string query = format_string("op=%s&volume_name=%s&snapshot_name=%s", op, esc_vol_name, esc_snap_name);
	rc = query_conductor(cfg, query, *this);
	if (rc != 0)
		return rc;

	for(int i=0;i<shards.size();i++){
		shards[i].parsed_store_ips = split_string(shards[i].store_ips, ',');
	}
	if(reopen) {
		state = VOLUME_OPENED;
		open_time = now_time_usec();
		return 0;
	}
	Cleaner clean;

	if(runtime_ctx == NULL) {
		if(is_aof){
			init_app_ctx(cfg, AOF_IODEPTH, 32, io_timeout);

			runtime_ctx = get_client_ctx();
		} else {
			// for g_app_ctx
			init_app_ctx(cfg, 0, 0, 0);
			// every volume has its own PfClientAppCtx
			runtime_ctx = new PfClientAppCtx();
			S5LOG_INFO("init context for volume:%s, iodepth:%d", volume_name.c_str(), io_depth);
			runtime_ctx->init(cfg, io_depth, 1, volume_id, io_timeout);
			assert(runtime_ctx->ref_count == 1);
			clean.push_back([this]() { runtime_ctx->dec_ref(); });
		}
	}
	event_queue = runtime_ctx->vol_proc->event_queue; //keep for quick reference
	state = VOLUME_OPENED;
	clean.cancel_all();
	open_time = now_time_usec();
	return 0;
}

static inline int cmp(const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}
struct conductor_entry{
	uint64_t update_time;
	std::string ip;
};
static std::map < std::string, conductor_entry> conductor_map;
static std::mutex conductor_map_lock;

#define CONDUCTOR_CACHE_TIMEOUT_US 60000000UL //60 seconds
void invalidate_conductor_ip_cache(const char* zk_host, const char* cluster_name)
{
	string cond_key = format_string("%s_%s", zk_host, cluster_name);

	const lock_guard<mutex> _l(conductor_map_lock);
	conductor_map.erase(cond_key);
}
string get_master_conductor_ip(const char *zk_host, const char* cluster_name)
{
	string cond_key = format_string("%s_%s", zk_host, cluster_name);
	{
		const lock_guard<mutex> _l(conductor_map_lock);
		auto it = conductor_map.find(cond_key);
		if(it != conductor_map.end() && it->second.update_time > now_time_usec() - CONDUCTOR_CACHE_TIMEOUT_US){
			return it->second.ip;
		}
	}
    struct String_vector condutors = {0};
    char **str = NULL;
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zhandle_t *zkhandle = zookeeper_init(zk_host, NULL, ZK_TIMEOUT, NULL, NULL, 0);
    if (zkhandle == NULL)
    {
        throw std::runtime_error("Error zookeeper_init");
    }
	DeferCall _r_z([zkhandle]() { zookeeper_close(zkhandle); });

	int state;
    for(int i=0; i<100; i++)
    {
		state = zoo_state(zkhandle);
        if (state == ZOO_CONNECTED_STATE)
            break;
		S5LOG_INFO("Connecting to zk server %s, state:%d ...", zk_host, state);
        usleep(300000);
    }
	if (state != ZOO_CONNECTED_STATE)
	{
		throw std::runtime_error("Error when connecting to zookeeper servers...");
	}

	int rc = 0;
	//const char* zk_root = "/pureflash/" + cluster_name;
	string zk_root = format_string("/pureflash/%s/conductors", cluster_name);
	rc = zoo_get_children(zkhandle, zk_root.c_str(), 0, &condutors);
	if (ZOK != rc || condutors.count == 0)
    {
		throw std::runtime_error(format_string("Error when get S5 conductor from zk, rc:%d, conductor count:%d", rc, condutors.count));
    }
	DeferCall _r_c([&condutors]() {deallocate_String_vector(&condutors); });
    str = (char **)condutors.data;
    qsort(str, condutors.count, sizeof(char *), cmp);

	char leader_path[256];
    int len = snprintf(leader_path, sizeof(leader_path), "%s/%s", zk_root.c_str(), str[0]);
    if (len >= sizeof(leader_path) || len < 0) {
		throw std::runtime_error(format_string("Cluster name is too long, max length is:%d", sizeof(leader_path)));
    }

	char ip_str[256];
	len = sizeof(ip_str);
	rc = zoo_get(zkhandle, leader_path, 0, ip_str, &len, 0);
    if (ZOK != rc) {
		throw std::runtime_error(format_string("Error when get pfconductor leader data:%d", rc));
    }
    if(len == 0 || len >= sizeof(ip_str)) {
		throw std::runtime_error(format_string("Error when get pfconductor leader data, invalid len:%d", len));
	}
    ip_str[len] = 0;
    //S5LOG_INFO("Get S5 conductor IP:%s", ip_str);

	{
		const lock_guard<mutex> _l(conductor_map_lock);
		conductor_entry e={now_time_usec(), ip_str};
		conductor_map[cond_key] = e;
	}

    return std::string(ip_str);
}

static size_t write_mem_callback(void *contents, size_t size, size_t nmemb, void *buf)
{
	size_t realsize = size * nmemb;
	curl_memory_t *mem = (curl_memory_t *)buf;

	mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL)
	{
		S5LOG_ERROR("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(mem->memory + mem->size, contents, realsize);
	mem->size += realsize;
	((char*)mem->memory)[mem->size] = '\0';

	return realsize;
}

void* pf_http_get(std::string& url, int timeout_sec, int retry_times)
{
	CURLcode res;
	CURL *curl = NULL;
	curl_memory_t curl_buf;
	curl = curl_easy_init();
	if (curl == NULL)
	{
		throw std::runtime_error("curl_easy_init failed.");
	}
	DeferCall _r_curl([curl]() { curl_easy_cleanup(curl); });

	curl_buf.memory = (char *)malloc(1 << 20);
	if (curl_buf.memory == NULL)
		throw std::runtime_error("Failed alloc curl buffer");
	curl_buf.size = 0;


	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&curl_buf);
	//curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// Set timeout.
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);

	for (int i = 0; i < retry_times; i++)
	{
		//S5LOG_DEBUG("Query %s ...", url.c_str());
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		res = curl_easy_perform(curl);
		if (res == CURLE_OK)
		{
			long http_code = 0;
			curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
			curl_buf.memory[curl_buf.size] = 0;
			//S5LOG_DEBUG("HTTP return:%d content:%*.s", http_code, curl_buf.size, curl_buf.memory);
			if (http_code == 200)
			{
				return curl_buf.memory;
			}
			else
			{
				S5LOG_ERROR("Server returns error, http code:%d, content:%s", http_code, curl_buf.memory);
				return NULL;
			}

		}
		if (i < retry_times - 1)
		{
			S5LOG_ERROR("Failed query %s, will retry", url.c_str());
			sleep(DEFAULT_HTTP_QUERY_INTERVAL);
		}
	}

	free(curl_buf.memory);
	return NULL;
}



void PfClientVolume::client_do_complete(int wc_status, BufferDescriptor* wr_bd)
{
	const static int ms1 = 1000;
	if (unlikely(wc_status != WC_SUCCESS))
	{//network failure has been handled by client_on_tcp_network_done
		S5LOG_ERROR("Internal error, Op complete unsuccessful opcode:%d, status:%s",
			wr_bd->wr_op, WcStatusToStr(WcStatus(wc_status)));
		return;
	}

    if (wr_bd->wr_op == TCP_WR_RECV || wr_bd->wr_op == RDMA_WR_RECV)
    {
		PfConnection* conn = wr_bd->conn;

	    PfClientIocb* io = wr_bd->client_iocb;
		PfMessageReply* reply = io->reply_bd->reply_bd;

			io->reply_time = now_time_usec();
			PfMessageStatus s = (PfMessageStatus)reply->status;
			if (unlikely(s & (MSG_STATUS_REOPEN)))
			{
				S5LOG_WARN( "Get reopen from store, conn:%s status code:0x%x, req meta_ver:%d store meta_ver:%d",
					conn->connection_info.c_str(), s, io->cmd_bd->cmd_bd->meta_ver, reply->meta_ver);
				//if (meta_ver < reply->meta_ver)
				{
					S5LOG_WARN("client meta_ver is:%d, store meta_ver is:%d. reopen volume", meta_ver, reply->meta_ver);
					event_queue->post_event(EVT_REOPEN_VOLUME, reply->meta_ver, (void *)(now_time_usec()), io->volume);
				}
				io->conn->dec_ref();
				io->conn=NULL;
				event_queue->post_event(EVT_IO_REQ, 0, io, this);
				runtime_ctx->reply_pool.free(wr_bd);
				return;
			}


			PfMessageHead* io_cmd = io->cmd_bd->cmd_bd;
			//On client side, we rely on the io timeout mechnism to release time connection
			//Here we just release the io task
			if (unlikely(io_cmd->opcode == S5_OP_HEARTBEAT))
			{
				__sync_fetch_and_sub(&conn->inflying_heartbeat, 1);
				io->sent_time = 0;
				io->conn->dec_ref();
				io->conn = NULL;
				runtime_ctx->free_iocb(io);
				runtime_ctx->reply_pool.free(wr_bd);
				return;
			}

			void* arg = io->ulp_arg;
			ulp_io_handler h = io->ulp_handler;
			uint64_t io_end_time = now_time_usec();
			uint64_t io_elapse_time = (io_end_time - io->submit_time) / ms1;

			if (io_elapse_time > 2000)
			{
				S5LOG_WARN("SLOW IO, shard id:%d, command_id:%d, vol:%s, since submit:%dms since send:%dms",
						   io_cmd->offset >> SHARD_SIZE_ORDER,
						   io_cmd->command_id,
						   volume_name.c_str(),
						   io_elapse_time,
						   (io_end_time-io->sent_time)/ms1
					);
			}


			runtime_ctx->reply_pool.free(io->reply_bd);
	        io->reply_bd = NULL;
	        io->sent_time=0;
	        io->conn->dec_ref();
	        io->conn=NULL;
	        if(io->cmd_bd->cmd_bd->opcode == S5_OP_READ)  {
	        	if(io->user_iov_cnt)
					iov_from_buf(io->user_iov, io->user_iov_cnt, io->data_bd->buf, io->data_bd->data_len);
		        else {
					//S5LOG_INFO("copy to user buffer!!!");
		        	memcpy(io->user_buf, io->data_bd->buf, io->data_bd->data_len);
				}
	        }
			runtime_ctx->free_iocb(io);
			if(s!=0){
				S5LOG_ERROR("IO complete in error:%d", s);
			}
			h(arg, s);

    }
    else if(wr_bd->wr_op != TCP_WR_SEND)
    {
       S5LOG_ERROR("Unexpected completion, op:%d", wr_bd->wr_op);
    }
}


void pf_close_volume(PfClientVolume* volume)
{
	volume->close();
	delete volume;
}

static int reopen_volume(PfClientVolume* volume)
{
	int rc = 0;
	S5LOG_INFO( "Reopening volume %s@%s, meta_ver:%d", volume->volume_name.c_str(),
		volume->snap_seq == -1 ? "HEAD" : volume->snap_name.c_str(), volume->meta_ver);

	volume->status = VOLUME_DISCONNECTED;
	rc = volume->do_open(true);
	if (unlikely(rc))
	{
		S5LOG_ERROR("Failed reopen volume!");
		volume->state = VOLUME_REOPEN_FAIL;
		return rc;
	} else {
		//TODO: notify volume has reopened, volume size may changed
	}
	volume->state = VOLUME_OPENED;

	S5LOG_INFO("Succeeded reopen volume %s@%s(0x%lx), meta_ver:%d io_depth=%d", volume->volume_name.c_str(),
		volume->snap_seq == -1 ? "HEAD" : volume->snap_name.c_str(), volume->volume_id, volume->meta_ver, volume->io_depth);
	PfClientIocb* io;
	while((io = volume->reopen_waiting.pop()) != NULL) {
		volume->resend_io(io);
	}
	return rc;
}

int PfClientVolume::resend_io(PfClientIocb* io)
{
	S5LOG_WARN("Requeue IO(cid:%d", io->cmd_bd->cmd_bd->command_id);
	io->cmd_bd->cmd_bd->command_seq ++;
	//__sync_fetch_and_add(&io->cmd_bd->cmd_bd->command_seq, 1);
	int rc = event_queue->post_event(EVT_IO_REQ, 0, io, this);
	if (rc)
		S5LOG_ERROR("Failed to resend_io io, rc:%d", rc);
	return rc;
}


const char* show_ver()
{
	return (const char*)pf_lib_ver;
}

int PfVolumeEventProc::process_event(int event_type, int arg_i, void* arg_p, void* arg_q)
{
	PfClientVolume* vol = (PfClientVolume*)arg_q;
	int rc = vol->process_event(event_type, arg_i, arg_p);
	if(rc == 0)
		return rc;
	switch(event_type){
		case EVT_SEND_HEARTBEAT:
			vol->runtime_ctx->heartbeat_once();
			break;
		default:
			S5LOG_ERROR("Volume get unknown event:%d", event_type);
	}
	return 0;
}

int PfClientVolume::process_event(int event_type, int arg_i, void* arg_p)
{
	//S5LOG_INFO("get event:%d", event_type);
	switch (event_type)
	{
	case EVT_IO_REQ:
	{
		PfClientIocb* io = (PfClientIocb*)arg_p;
		PfMessageHead *io_cmd = io->cmd_bd->cmd_bd;

		int shard_index = (int)(io_cmd->offset >> SHARD_SIZE_ORDER);
#ifdef WITH_PFS2
		if(shards[shard_index].is_local){
			PfClientStore* local_store = get_local_store(shard_index);
			if(local_store == NULL){
				io->ulp_handler(io->ulp_arg, PfMessageStatus::MSG_STATUS_CONN_LOST);
			}
			io->setup_subtask(io_cmd->opcode);
			if (io_cmd->opcode == S5_OP_READ)
				local_store->do_read(&io->io_subtasks[0]);
			else if (io_cmd->opcode == S5_OP_WRITE)
				local_store->do_write(&io->io_subtasks[0]);
			else {
				S5LOG_FATAL("Invalid op :%d", io_cmd->opcode);
			}
			local_store->ioengine->submit_batch();
		} else 
#endif
		{
			BufferDescriptor* cmd_bd = io->cmd_bd;
			struct PfConnection* conn = get_shard_conn(shard_index);

			if (unlikely(conn == NULL))
			{//no server available, volume will reopen soon
				if (now_time_usec() > io->submit_time + SEC2US(runtime_ctx->io_timeout)) {
					io->ulp_handler(io->ulp_arg, -EIO);
					S5LOG_ERROR("IOError, can't get a usable connection before timeout, volume:%s command id:%d task_sequence:%d, io_cmd :%d",
						volume_name.c_str(), io_cmd->command_id, io_cmd->command_seq, io_cmd->opcode);
					runtime_ctx->iocb_pool.free(io);

				}
				else {
					reopen_waiting.append(io);
				}
				if (state == VOLUME_OPENED) {
					state = VOLUME_WAIT_REOPEN;
					std::ignore = std::async(std::launch::async, [this]() {
						S5LOG_INFO("Will reopen volume %s after 5s ...", volume_name.c_str());
						sleep(5);
						event_queue->post_event(EVT_REOPEN_VOLUME, meta_ver, (void*)(now_time_usec()), this);
						});
				}

				break;
			}
			io_cmd->meta_ver = (uint16_t)meta_ver;

			BufferDescriptor* rbd = runtime_ctx->reply_pool.alloc();
			if(unlikely(rbd == NULL))
			{
				S5LOG_ERROR("No reply bd available to do io now, requeue IO");
				io->ulp_handler(io->ulp_arg, -EAGAIN);
				runtime_ctx->iocb_pool.free(io);
				break;
			}
			io->is_timeout = FALSE;
			io->conn = conn;
			conn->add_ref();
			io->sent_time = now_time_usec();
			int rc = conn->post_recv(rbd);
			if (rc)
			{
				S5LOG_ERROR("Failed to post_recv for reply");
				runtime_ctx->reply_pool.free(rbd);
				io->ulp_handler(io->ulp_arg, -EAGAIN);
				runtime_ctx->iocb_pool.free(io);
				break;
			}
			// set rkey if rdma;  TODO: move this operation into PfRdmaConnection::post_send
			if (client_conn_type == RDMA_TYPE)
				cmd_bd->cmd_bd->rkey = io->data_bd->mrs[((PfRdmaConnection*)conn)->dev_ctx->idx]->rkey;
			rc = conn->post_send(cmd_bd);
			if (rc)	{
				S5LOG_ERROR("Failed to post_send for cmd");
				//reply_pool.free(rbd); //not reclaim rbd, since this bd has in connection's receive queue
				io->ulp_handler(io->ulp_arg, -EAGAIN);
				runtime_ctx->iocb_pool.free(io);
				break;
			}
		}
		break;
	}
	case EVT_IO_COMPLETE:
		client_do_complete(arg_i, (BufferDescriptor*)arg_p);
		break;
	case EVT_IO_TIMEOUT:
	{
		if (state != VOLUME_OPENED)
		{
			S5LOG_WARN("volume state is:%d", state);
			break;
		}
		PfClientIocb* io = (PfClientIocb*)arg_p;
		//S5LOG_WARN("volume_proc timeout, cid:%d, conn:%s", io->cmd_bd->cmd_bd->command_id, io->conn->connection_info.c_str());
		/*
		 * If time_recv is 0, io task:1)does not begin, 2)has finished.
		 */
		if (io->sent_time != 0 && now_time_usec() > io->sent_time + runtime_ctx->io_timeout * 1000000LL)
		{
			PfMessageHead *io_cmd = (PfMessageHead *)io->cmd_bd->buf;
			if (unlikely(io_cmd->opcode == S5_OP_HEARTBEAT))
			{
				S5LOG_ERROR("heartbeat timeout for conn:%s", io->conn->connection_info.c_str());
				runtime_ctx->iocb_pool.free(io);
				break;
			}
			S5LOG_WARN("IO(cid:%d) timeout, vol:%s, shard:%d, store:%s will reconnect and resend...",
				io_cmd->command_id, volume_name.c_str(), io_cmd->offset >> SHARD_SIZE_ORDER, io->conn->connection_info.c_str());
			io->sent_time = 0;
			io->conn->close();
			io->conn->dec_ref();
			io->conn = NULL;
			int rc = resend_io(io);
			if(rc) {
				runtime_ctx->iocb_pool.free(io);
			}
		}
		break;
	}
	case EVT_REOPEN_VOLUME:
	{
		if ((uint64_t)arg_p > open_time)
		{
			reopen_volume(this);
		}
		break;
	}
	case EVT_VOLUME_RECONNECT:
	{
		if (runtime_ctx->iocb_pool.remain() == runtime_ctx->iocb_pool.obj_count)
			if (state == VOLUME_OPENED || state == VOLUME_REOPEN_FAIL || state == VOLUME_DISCONNECTED)
			{
				S5LOG_WARN("send volume reopen request for:%s", volume_name.c_str());
				event_queue->post_event(EVT_REOPEN_VOLUME, 0, (void*)(now_time_usec()), this);
			}
		break;
	}
	default:
		return 1;
	}
	return 0;
}

/**
* get a shard connection from pool. connection is shared by shards on same node.
*/
PfConnection* PfClientVolume::get_shard_conn(int shard_index)
{
	PfConnection* conn = NULL;
	if (state != VOLUME_OPENED)
	{
		return NULL;
	}
	PfClientShardInfo * shard = &shards[shard_index];
	for (int i=0; i < shard->parsed_store_ips.size(); i++)
	{
		conn = runtime_ctx->conn_pool->get_conn(shard->parsed_store_ips[shard->current_ip], client_conn_type);
		if (conn != NULL) {
			if (client_conn_type == RDMA_TYPE && !runtime_ctx->mr_registered) {
				runtime_ctx->PfRdmaRegisterMr(((PfRdmaConnection *)conn)->dev_ctx);
			}
			return conn;
		}
		shard->current_ip = (shard->current_ip + 1) % (int)shard->parsed_store_ips.size();
	}
	S5LOG_ERROR("Failed to get an usable IP for vol:%s shard:%d, change volume state to VOLUME_DISCONNECTED ",
			 volume_name.c_str(), shard_index);
	return NULL;
}

void PfClientVolume::close() {
	S5LOG_INFO("close volume:%s", volume_name.c_str());
	state = VOLUME_CLOSED;


	runtime_ctx->remove_volume(this);
	runtime_ctx->dec_ref();
//	for(int i=0;i<volume->shards.size();i++)
//	{
//		delete volume->shards[i];
//	}
}
static inline size_t
iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt, void *buf, size_t bytes)
{
	size_t done;
	size_t offset = 0;
	unsigned int i;
	for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
		if (offset < iov[i].iov_len) {
			size_t len = std::min(iov[i].iov_len - offset, bytes - done);
			memcpy((char*)buf + done, (char*)iov[i].iov_base + offset, len);
			done += len;
			offset = 0;
		} else {
			offset -= iov[i].iov_len;
		}
	}
	assert(offset == 0);
	return done;
}
size_t iov_from_buf(const struct iovec *iov, unsigned int iov_cnt, const void *buf, size_t bytes)
{
	size_t done;
	size_t offset = 0;
	unsigned int i;
	for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
		if (offset < iov[i].iov_len) {
			size_t len = std::min(iov[i].iov_len - offset, bytes - done);
			memcpy((char*)iov[i].iov_base + offset, (char*)buf + done, len);
			done += len;
			offset = 0;
		} else {
			offset -= iov[i].iov_len;
		}
	}
	assert(offset == 0);
	return done;
}
static int unalign_io_print_cnt = 0;
int pf_iov_submit(struct PfClientVolume* volume, const struct iovec *iov, const unsigned int iov_cnt, size_t length, off_t offset,
                 ulp_io_handler callback, void* cbk_arg, int is_write) {
	// Check request params
	if (unlikely((offset & SECT_SIZE_MASK) != 0 || (length & SECT_SIZE_MASK) != 0 )) {
		S5LOG_ERROR("Invalid offset:%ld or length:%ld", offset, length);
		return -EINVAL;
	}
	if(unlikely(length > PF_MAX_IO_SIZE)){
		S5LOG_ERROR("IO size:%ld exceed max:%ld", length, PF_MAX_IO_SIZE);
		return -EINVAL;
	}
	if(unlikely((offset & 0x0fff) || (length & 0x0fff)))	{
		unalign_io_print_cnt ++;
		if((unalign_io_print_cnt % 1000) == 1) {
			S5LOG_WARN("Unaligned IO on volume:%s OP:%s offset:0x%lx len:0x%x, num:%d", volume->volume_name.c_str(),
			        is_write?"WRITE":"READ", offset, length, unalign_io_print_cnt);
		}
	}
	auto io = volume->runtime_ctx->iocb_pool.alloc();
	if (io == NULL) {
		S5LOG_WARN("IOCB pool empty, EAGAIN!");
		return -EAGAIN;
	}
	//S5LOG_INFO("Alloc iocb:%p, data_bd:%p", io, io->data_bd);
	//assert(io->data_bd->client_iocb != NULL);
	io->volume = volume;
	io->ulp_handler = callback;
	io->ulp_arg = cbk_arg;

	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;
	io->submit_time = now_time_usec();
	io->user_iov = iov;
	io->user_iov_cnt = iov_cnt;
	io->data_bd->data_len = (int)length;
	cmd->opcode = is_write ? S5_OP_WRITE : S5_OP_READ;
	if(is_write){
		iov_to_buf(iov, iov_cnt, io->data_bd->buf, length);
	}
	cmd->vol_id = volume->volume_id;
	cmd->buf_addr = (__le64)io->data_bd->buf;
	cmd->rkey = 0;
	cmd->offset = offset;
	cmd->length = (uint32_t)length;
	cmd->snap_seq = volume->snap_seq;
	int rc = volume->event_queue->post_event( EVT_IO_REQ, 0, io, volume);
	if (rc)
		S5LOG_ERROR("Failed to submmit io, rc:%d", rc);
	return rc;
}

int pf_io_submit(struct PfClientVolume* volume, void* buf, size_t length, off_t offset,
                 ulp_io_handler callback, void* cbk_arg, int is_write) {
	// Check request params
	if (unlikely((offset & SECT_SIZE_MASK) != 0 || (length & SECT_SIZE_MASK) != 0 )) {
		S5LOG_ERROR("Invalid offset:%ld or length:%ld", offset, length);
		return -EINVAL;
	}
	if(unlikely((offset & 0x0fff) || (length & 0x0fff)))	{
		unalign_io_print_cnt ++;
		if((unalign_io_print_cnt % 1000) == 1) {
			S5LOG_WARN("Unaligned IO on volume:%s OP:%s offset:0x%lx len:0x%x, num:%d", volume->volume_name.c_str(),
			           is_write ? "WRITE" : "READ", offset, length, unalign_io_print_cnt);
		}
	}
	auto io = volume->runtime_ctx->iocb_pool.alloc();
	if (io == NULL){
		S5LOG_WARN("IOCB pool empty, EAGAIN!");
		return -EAGAIN;
	}
	//S5LOG_INFO("Alloc iocb:%p, data_bd:%p", io, io->data_bd);
	//assert(io->data_bd->client_iocb != NULL);
	io->volume = volume;
	io->ulp_handler = callback;
	io->ulp_arg = cbk_arg;

	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;
	io->submit_time = now_time_usec();
	io->user_buf = buf;
	io->user_iov_cnt = 0;
	memcpy(io->data_bd->buf, buf, length);
	io->data_bd->data_len = (int)length;
	cmd->opcode = is_write ? S5_OP_WRITE : S5_OP_READ;
	cmd->vol_id = volume->volume_id;
	cmd->buf_addr = (__le64)io->data_bd->buf;
	cmd->rkey = 0;
	cmd->offset = offset;
	cmd->length = (uint32_t)length;
	cmd->snap_seq = volume->snap_seq;
	int rc = volume->event_queue->post_event( EVT_IO_REQ, 0, io, volume);
	if (rc)
		S5LOG_ERROR("Failed to submmit io, rc:%d", rc);
	return rc;
}

uint64_t pf_get_volume_size(struct PfClientVolume* vol)
{
	return vol->volume_size;
}

int pf_create_tenant(const char* tenant_name, const char* cfg_filename)
{
	int rc = 0;
	
	S5LOG_INFO("Creating tenant %s", tenant_name);
	try
	{
		Cleaner _clean;
		GeneralReply reply;

		if (cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		conf_file_t cfg = conf_open(cfg_filename);
		if (cfg == NULL)
		{
			S5LOG_ERROR("Failed open config file:%s, rc:%d", cfg_filename, -errno);
			return -errno;
		}
		DeferCall _cfg_r([cfg]() { conf_close(cfg); });

		char* esc_t_name = curl_easy_escape(NULL, tenant_name, 0);
		if (!esc_t_name)
		{
			S5LOG_ERROR("Curl easy escape failed. ENOMEM");
			return -ENOMEM;
		}
		DeferCall _1([esc_t_name]() { curl_free(esc_t_name); });
		std::string query = format_string("op=create_tenant&tenant_name=%s", esc_t_name);

		rc = query_conductor(cfg, query, reply);
		if (rc != 0)
		{
			S5LOG_ERROR("Failed query conductor, rc:%d", rc);
			return rc;
		}

		
		S5LOG_INFO("Succeeded create tenant %s", tenant_name);

		_clean.cancel_all();
		return 0;
	}
	catch (std::exception& e)
	{
		S5LOG_ERROR("Exception in create tenant:%s", e.what());
	}
	return -1;
}

int pf_rename_volume(/*const char* tenant_name,*/const char* vol_name, const char* new_name, const char* cfg_filename)
{
	int rc = 0;

	S5LOG_INFO("rename volume %s to %s", vol_name, new_name);
	try
	{
		GeneralReply reply;

		if (cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		conf_file_t cfg = conf_open(cfg_filename);
		if (cfg == NULL)
		{
			S5LOG_ERROR("Failed open config file:%s, rc:%d", cfg_filename, -errno);
			return -errno;
		}
		DeferCall _cfg_r([cfg]() { conf_close(cfg); });

		//char* esc_t_name = curl_easy_escape(NULL, tenant_name, 0);
		//if (!esc_t_name)
		//{
		//	S5LOG_ERROR("Curl easy escape failed. ENOMEM");
		//	return -ENOMEM;
		//}
		//DeferCall _1([esc_t_name]() { curl_free(esc_t_name); });

		char* esc_v_name = curl_easy_escape(NULL, vol_name, 0);
		if (!esc_v_name)
		{
			S5LOG_ERROR("Curl easy escape failed. ENOMEM");
			return -ENOMEM;
		}
		DeferCall _2([esc_v_name]() { curl_free(esc_v_name); });

		char* esc_nv_name = curl_easy_escape(NULL, new_name, 0);
		if (!esc_nv_name)
		{
			S5LOG_ERROR("Curl easy escape failed. ENOMEM");
			return -ENOMEM;
		}
		DeferCall _3([esc_nv_name]() { curl_free(esc_nv_name); });


		//std::string query = format_string("op=update_volume&tenant_name=%s&volume_name=%s&new_volume_name=%s",
		//	esc_t_name, esc_v_name, esc_nv_name);
		std::string query = format_string("op=update_volume&volume_name=%s&new_volume_name=%s",
			esc_v_name, esc_nv_name);

		rc = query_conductor(cfg, query, reply);
		if (rc != 0)
		{
			S5LOG_ERROR("Failed query conductor, rc:%d", rc);
			return rc;
		}


		S5LOG_INFO("Succeeded rename volume %s", vol_name);
		return 0;
	}
	catch (std::exception& e)
	{
		S5LOG_ERROR("Exception in rename volume:%s", e.what());
	}
	return -1;
}

int pf_delete_volume(const char* vol_name,  const char* cfg_filename)
{
	int rc = 0;

	S5LOG_INFO("delete volume %s ", vol_name);
	try
	{
		GeneralReply reply;

		if (cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		conf_file_t cfg = conf_open(cfg_filename);
		if (cfg == NULL)
		{
			S5LOG_ERROR("Failed open config file:%s, rc:%d", cfg_filename, -errno);
			return -errno;
		}
		DeferCall _cfg_r([cfg]() { conf_close(cfg); });

		//char* esc_t_name = curl_easy_escape(NULL, tenant_name, 0);
		//if (!esc_t_name)
		//{
		//	S5LOG_ERROR("Curl easy escape failed. ENOMEM");
		//	return -ENOMEM;
		//}
		//DeferCall _1([esc_t_name]() { curl_free(esc_t_name); });

		char* esc_v_name = curl_easy_escape(NULL, vol_name, 0);
		if (!esc_v_name)
		{
			S5LOG_ERROR("Curl easy escape failed. ENOMEM");
			return -ENOMEM;
		}
		DeferCall _2([esc_v_name]() { curl_free(esc_v_name); });

		
		std::string query = format_string("op=delete_volume&volume_name=%s", esc_v_name);

		rc = query_conductor(cfg, query, reply);
		if (rc != 0)
		{
			S5LOG_ERROR("Failed query conductor, rc:%d", rc);
			return rc;
		}


		S5LOG_INFO("Succeeded delete volume %s", vol_name);
		return 0;
	}
	catch (std::exception& e)
	{
		S5LOG_ERROR("Exception in delete volume:%s", e.what());
	}
	return -1;
}

void PfClientAppCtx::remove_volume(PfClientVolume* vol)
{
	const std::lock_guard<std::mutex> lock(opened_volumes_lock);
	opened_volumes.remove(vol);
}
void PfClientAppCtx::add_volume(PfClientVolume* vol)
{
	const std::lock_guard<std::mutex> lock(opened_volumes_lock);
	opened_volumes.push_back(vol);
}

PfClientAppCtx::~PfClientAppCtx()
{
	while (iocb_pool.obj_count != vol_proc->sync_invoke([this]() {
		return iocb_pool.free_obj_queue.count();
		})) {
		S5LOG_INFO("Waiting inflight IO to complete...");
		usleep(10000);
	}
	
	pthread_cancel(timeout_thread.native_handle());
	timeout_thread.join();
	vol_proc->stop();
	conn_pool->close_all();
	delete tcp_poller; //will call destroy inside
	if (client_conn_type == RDMA_TYPE && mr_registered)
		PfRdmaUnRegisterMr();
	iocb_pool.destroy();
	cmd_pool.destroy();
	data_pool.destroy();
	reply_pool.destroy();
	sem_destroy(&io_throttle);
}

void PfClientAppCtx::timeout_check_proc()
{
	prctl(PR_SET_NAME, "clnt_time_chk");
	while (1)
	{
		if (sleep(CLIENT_TIMEOUT_CHECK_INTERVAL) != 0)
			return;

		uint64_t now = now_time_usec();
		struct PfClientIocb* ios = iocb_pool.data;
		int64_t io_timeout_us = io_timeout * 1000000LL;
		for (int i = 0; i < iocb_pool.obj_count; i++)
		{
			if (ios[i].sent_time != 0 && now > ios[i].sent_time + io_timeout_us && ios[i].is_timeout != 1)
			{
			//IO may has been timeout, but not accurate, send to volume thread to judge again
				//S5LOG_DEBUG("IO timeout detected, cid:%d, volume:%s, timeout:%luus",
				//	((PfMessageHead*)ios[i].cmd_bd->buf)->command_id, ios[i].volume->volume_name.c_str(), io_timeout_us);
				ios[i].volume->event_queue->post_event(EVT_IO_TIMEOUT, 0, &ios[i], ios[i].volume);
				ios[i].is_timeout = 1;
			}
		}
		//opened_volumes_lock.lock();
		//for(auto vol : opened_volumes){
		//	vol->check_io_timeout();
		//}
		//opened_volumes_lock.unlock();
	}

}

void PfClientAppCtx::heartbeat_once()
{
	if (conn_pool->ip_id_map.size() == 0)
	{
		return;
	}
	next_heartbeat_idx %= (int)conn_pool->ip_id_map.size();
	int hb_sent = 0;
	int ht_idx = 0;
	for (auto it = conn_pool->ip_id_map.begin(); it != conn_pool->ip_id_map.end();)
	{

		PfConnection* conn = it->second;
		if (conn->state != CONN_OK || __sync_fetch_and_sub(&conn->inflying_heartbeat, 0) > 2)
		{
			it = conn_pool->ip_id_map.erase(it);
			S5LOG_ERROR("connection:%p:%s timeout", conn, conn->connection_info.c_str());
			conn->close();
		}
		else
		{
			++it;

			if (ht_idx >= next_heartbeat_idx && hb_sent < 4)
			{
				conn->send_heartbeat();
				hb_sent++;
				next_heartbeat_idx++;
			}
		}
		ht_idx++;
	}
}

int PfClientAppCtx::PfRdmaRegisterMr(struct PfRdmaDevContext* dev_ctx)
{
	struct ibv_pd* pd = dev_ctx->pd;
	int idx = dev_ctx->idx;

	S5LOG_INFO("client register memory region!!");

	cmd_pool.rmda_register_mr(pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	data_pool.rmda_register_mr(pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	reply_pool.rmda_register_mr(pd, idx, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

	mr_registered = true;
	return 0;
}

void PfClientAppCtx::PfRdmaUnRegisterMr()
{
	S5LOG_INFO("client unregister memory region!!");

	cmd_pool.rmda_unregister_mr();
	data_pool.rmda_unregister_mr();
	reply_pool.rmda_unregister_mr();
	mr_registered = false;

	return;
}

void rpc_cbk(void* arg, int status)
{
	sem_t *sem = (sem_t*)arg;
	sem_post(sem);
}
int PfClientAppCtx::rpc_common(PfClientVolume* vol, std::function<void(PfMessageHead* req_cmd)> head_filler,
	std::function<void(PfMessageReply* reply)> reply_extractor)
{
	int rc;
	sem_t sem;
	sem_init(&sem, 0, 0);
	PfClientIocb* io = iocb_pool.alloc();
	io->ulp_handler = rpc_cbk;
	io->ulp_arg = &sem;
	BufferDescriptor* cmd_bd = io->cmd_bd;
	PfMessageHead* io_cmd = io->cmd_bd->cmd_bd;
	head_filler(io_cmd);

	PfConnection* conn;
	BufferDescriptor* rbd;
	//struct timespec ts;


	rc = 0;
	conn = conn_pool->get_conn(vol->owner_ip, TCP_TYPE);
	if (conn == NULL)
		S5LOG_ERROR("Failed to get connection to owner:%s", vol->owner_ip);


	io_cmd->meta_ver = (uint16_t)vol->meta_ver;

	rbd = reply_pool.alloc();
	if (unlikely(rbd == NULL)) {
		S5LOG_ERROR("No reply bd available to do io now, requeue IO");
		io->ulp_handler(io->ulp_arg, -EAGAIN);
		iocb_pool.free(io);
		return -ENOMEM;
	}
	io->is_timeout = FALSE;
	io->conn = conn;
	conn->add_ref();
	io->sent_time = now_time_usec();
	rc = conn->post_recv(rbd);
	if (rc)
	{
		S5LOG_ERROR("Failed to post_recv for reply");
		reply_pool.free(rbd);
		iocb_pool.free(io);
		return rc;
	}
	rc = conn->post_send(cmd_bd);
	if (rc) {
		S5LOG_ERROR("Failed to post_send for cmd");
		//reply_pool.free(rbd); //not reclaim rbd, since this bd has in connection's receive queue
		iocb_pool.free(io);
		return rc;
	}


	//clock_gettime(CLOCK_REALTIME, &ts);
	//ts.tv_sec += RPC_TIMEOUT_SEC;

	//if(sem_timedwait(&sem, &ts) == -1){
	//	rc = -errno;
	//	S5LOG_ERROR("RPC wait failed, rc:%d", rc);
	//	if(rc == ETIMEDOUT){
	//		vol->owner_ip.clean();
	//		goto retry_owner;
	//	}
	//} else {
	//	rc = (int)io->reply_bd->reply_bd->block_id;
	//}

	sem_wait(&sem);

	rc = io->reply_bd->reply_bd->status;
	if( rc == PfMessageStatus::MSG_STATUS_SUCCESS) {
		reply_extractor(io->reply_bd->reply_bd);
	}
	
	reply_pool.free(io->reply_bd);
	io->reply_bd = NULL;
	io->sent_time = 0;
	io->conn->dec_ref();
	io->conn = NULL;
	free_iocb(io);
	return rc;
}
int PfClientAppCtx::rpc_alloc_block(PfClientVolume* volume, uint64_t offset)
{
	int new_block_id = 0;
	int rc;
	rc = rpc_common(volume, [volume, offset](PfMessageHead* cmd){
		cmd->opcode = S5_OP_RPC_ALLOC_BLOCK;
		cmd->vol_id = volume->volume_id;
		cmd->rkey = 0;
		cmd->offset = offset;
		cmd->snap_seq = volume->snap_seq;
		},
		[&new_block_id](PfMessageReply* reply) {
			new_block_id = reply->block_id;
		}
	);
	if(rc == 0)
		return new_block_id;
	return rc;
}

int PfClientAppCtx::rpc_delete_obj(PfClientVolume* volume, uint64_t slba, uint32_t snap_seq)
{
	int rc;
	rc = rpc_common(volume, [volume, slba](PfMessageHead* cmd) {
		cmd->opcode = S5_OP_RPC_DELETE_BLOCK;
		cmd->vol_id = volume->volume_id;
		cmd->rkey = 0;
		cmd->offset = slba;
		cmd->snap_seq = volume->snap_seq;
		},
		[](PfMessageReply* reply) {
			
		}
		);
	return rc;
}

#ifdef WITH_PFS2
static std::unordered_map<std::string, PfClientStore*> local_stores;
PfClientStore* PfClientVolume::get_local_store(int shard_index)
{
	
	PfClientShardInfo* shard = &shards[shard_index];
	if(shard->local_store == NULL){
		auto pos = local_stores.find(shard->local_dev_uuid);
		if(pos == local_stores.end()){
			PfClientStore* store = new PfClientStore();
			int rc = shard->local_store->init(this, shard->local_dev_name.c_str(), shard->local_dev_uuid.c_str());
			if(rc){
				S5LOG_ERROR("Failed to init local store on dev:%s, rc:%d", shard->local_dev_name.c_str(), rc);
				delete store;
				return NULL;
			}
			local_stores[shard->local_dev_uuid] = store;
			shard->local_store = store;
		} else {
			shard->local_store = pos->second;
		}
	}
	return shard->local_store;
}
#endif
static void __attribute__((constructor)) spdk_engine_init(void)
{
	spdk_engine_set(false);
}

