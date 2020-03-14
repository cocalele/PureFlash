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

#include "s5_connection_pool.h"
#include "s5_connection.h"
#include "s5_client_priv.h"
#include <nlohmann/json.hpp>
#include "s5message.h"
#include "s5_poller.h"
#include "s5_buffer.h"

using namespace std;
using nlohmann::json;

#define S5_LIB_VER 0x00010000
static const char* s5_lib_ver = "S5 client version:0x00010000";

#define CLIENT_TIMEOUT_CHECK_INTERVAL 1 //seconds

void from_json(const json& j, S5ClientShardInfo& p) {
	j.at("index").get_to(p.index);
	j.at("store_ips").get_to(p.store_ips);

}
void from_json(const json& j, S5ClientVolumeInfo& p) {
	j.at("status").get_to(p.status);
	j.at("volume_name").get_to(p.volume_name);
	j.at("volume_size").get_to(p.volume_size);
	j.at("volume_id").get_to(p.volume_id);
	j.at("shard_count").get_to(p.shard_count);
	j.at("rep_count").get_to(p.rep_count);
	j.at("meta_ver").get_to(p.meta_ver);
	j.at("snap_seq").get_to(p.snap_seq);
	j.at("shards").get_to(p.shards);
}

template<typename ReplyT>
static int query_conductor(conf_file_t cfg, const string& query_str, ReplyT& reply);

#define MAX_URL_LEN 2048
#define DEFAULT_TIME_INTERVAL 3

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

struct S5ClientVolumeInfo* s5_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,
	int lib_ver)
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
		S5ClientVolumeInfo* volume = new S5ClientVolumeInfo;
		if (volume == NULL)
		{
			S5LOG_ERROR("alloca memory for volume failed!");
			return NULL;
		}
		_clean.push_back([volume]() { delete volume; });
		//other calls
		volume->volume_name = volume_name;
		volume->cfg_file = cfg_filename;
		volume->snap_name = snap_name;

		rc = volume->do_open();
		if (rc)	{
			return NULL;
		}


		volume->timeout_thread = std::thread([volume] (){
			volume->timeout_check_proc();
		});

		volume->vol_proc = new S5VolumeEventProc(volume);
		volume->vol_proc->start();

		volume->state = VOLUME_OPENED;
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

static int client_on_work_complete(BufferDescriptor* bd, WcStatus complete_status, S5Connection* conn, void* cbk_data)
{
	return conn->volume->vol_proc->event_queue.post_event(EVT_IO_COMPLETE, complete_status, bd);
}


int S5ClientVolumeInfo::do_open()
{
	int rc = 0;
	conf_file_t cfg = conf_open(cfg_file.c_str());
	if(cfg == NULL)
	{
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });
	io_depth = conf_get_int(cfg, "client", "io_depth", 32, FALSE);
	io_timeout = conf_get_int(cfg, "client", "io_timeout", 30, FALSE);

	char* esc_vol_name = curl_easy_escape(NULL, volume_name.c_str(), 0);
	if (!esc_vol_name)
	{
		throw runtime_error("Curl easy escape failed.");
	}
	DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });
	char* esc_snap_name = curl_easy_escape(NULL, snap_name.c_str(), 0);
	if (!esc_snap_name)
	{
		throw runtime_error("Curl easy escape failed.");
	}
	DeferCall _2([esc_snap_name]() { curl_free(esc_snap_name); });

	std::string query = format_string("/op=open_volume&volume_name=%s&snap_name=%s", esc_vol_name, esc_snap_name);
	rc = query_conductor(cfg, query, *this);
	if (rc != 0)
		return rc;

	Cleaner clean;
	tcp_poller = new S5Poller();
	if(tcp_poller == NULL)
		throw runtime_error("No memory to alloc poller");

	conn_pool = new S5ConnectionPool();
	if (conn_pool == NULL)
		throw runtime_error("No memory to alloc connection pool");
	conn_pool->init((int)shards.size()*2, tcp_poller, io_depth, client_on_work_complete);
	data_pool.init(S5_MAX_IO_SIZE, io_depth);
	cmd_pool.init(sizeof(s5_message_head), io_depth);
	reply_pool.init(sizeof(s5_message_reply), io_depth);
	iocb_pool.init(io_depth);
	for(int i=0;i<io_depth;i++)
	{
		S5ClientIocb* io = iocb_pool.alloc();
		io->cmd_bd = cmd_pool.alloc();
		io->data_bd = data_pool.alloc();
		iocb_pool.free(io);
	}
	open_time = now_time_usec();
	return 0;
}



static inline int cmp(const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}

static string get_master_conductor_ip(const char *zk_host)
{
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
		S5LOG_ERROR("Connecting to zk server %s, state:%d ...", zk_host, state);
        usleep(300000);
    }
	if (state != ZOO_CONNECTED_STATE)
	{
		throw std::runtime_error("Error when connecting to zookeeper servers...");
	}

	const char* zk_root = "/s5/conductors";
    if (ZOK != zoo_get_children(zkhandle, zk_root, 0, &condutors) || condutors.count == 0)
    {
		throw std::runtime_error("Error when get S5 conductor children from zk...");
    }
	DeferCall _r_c([&condutors]() {deallocate_String_vector(&condutors); });
    str = (char **)condutors.data;
    qsort(str, condutors.count, sizeof(char *), cmp);

	char leader_path[256];
    int len = snprintf(leader_path, sizeof(leader_path), "%s/%s", zk_root, str[0]);
    if (len >= sizeof(leader_path) || len < 0)
    {
		throw std::runtime_error(format_string("Cluster name is too long, max length is:%d", sizeof(leader_path)));
    }

	char ip_str[256];
	len = sizeof(ip_str);
    if (ZOK != zoo_get(zkhandle, leader_path, 0, ip_str, &len, 0))
    {
		throw std::runtime_error("Error when get S5 conductor leader data...");
    }
    S5LOG_INFO("Get S5 conductor IP:%s", ip_str);
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

template<typename ReplyT>
static int query_conductor(conf_file_t cfg, const string& query_str, ReplyT& reply)
{
	char zk_servers[1024] = { 0 };

	const char* zk_ip = conf_get(cfg, "zookeeper", "ip", "", TRUE);
	if(zk_ip == NULL || strlen(zk_servers) == 0)
    {
		throw std::runtime_error("zookeeper ip not found in conf file");
    }


	CURLcode res;
	CURL *curl = NULL;
	curl_memory_t curl_buf;
	curl_buf.memory = (char *)malloc(1 << 20);
	if (curl_buf.memory == NULL)
		throw std::runtime_error("Failed alloc curl buffer");
	curl_buf.size = 0;
	DeferCall _fb([&curl_buf]() {free(curl_buf.memory); });
	int open_volume_timeout = conf_get_int(cfg, "client", "open_volume_timeout", 30, FALSE);

	curl = curl_easy_init();
	if (curl == NULL)
	{
		throw std::runtime_error("curl_easy_init failed.");
	}
	DeferCall _r_curl([curl]() { curl_easy_cleanup(curl); });

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&curl_buf);
	//curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// Set timeout.
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, open_volume_timeout);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
	char url[MAX_URL_LEN] = { 0 };


	int retry_times = 5;
	for (int i = 0; i < retry_times; i++)
	{
		std::string conductor_ip = get_master_conductor_ip(zk_ip);

		sprintf(url, "http://%s:49180/s5c/?%s", conductor_ip.c_str(), query_str.c_str());
		S5LOG_DEBUG("Query %s ...", url);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		res = curl_easy_perform(curl);
		if (res == CURLE_OK)
		{
			curl_buf.memory[curl_buf.size] = 0;
			auto j = json::parse(curl_buf.memory);
			j.get_to<ReplyT>(reply);
			return 0;
		}
		if (i < retry_times - 1)
		{
			S5LOG_ERROR("Failed query %s, will retry", url);
			sleep(DEFAULT_TIME_INTERVAL);
		}
	}

	return -1;
}

void S5ClientVolumeInfo::free_iocb(S5ClientIocb* iocb)
{
	if(iocb->cmd_bd != NULL) {
		iocb->cmd_bd->conn->dec_ref();
		cmd_pool.free(iocb->cmd_bd);
		iocb->cmd_bd = NULL;
	}
	if(iocb->data_bd != NULL) {
		iocb->data_bd->conn->dec_ref();
		data_pool.free(iocb->data_bd);
		iocb->data_bd = NULL;
	}
	if(iocb->reply_bd != NULL) {
		iocb->reply_bd->conn->dec_ref();
		reply_pool.free(iocb->reply_bd);
		iocb->reply_bd = NULL;
	}
}

void S5ClientVolumeInfo::client_do_complete(int wc_status, BufferDescriptor* wr_bd)
{
	if (unlikely(wc_status != TCP_WC_SUCCESS))
	{
		S5LOG_INFO("Op complete unsuccessful opcode:%d, status:%d", wr_bd->wr_op, wc_status);

		wr_bd->conn->dec_ref();
		S5Connection* conn = wr_bd->conn;
		reply_pool.free(wr_bd); //this connection should be closed
		conn->close();
		return;
	}

    if (wr_bd->wr_op == TCP_WR_RECV)
    {
		S5Connection* conn = wr_bd->conn;
		S5ClientVolumeInfo* vol = conn->volume;
		struct s5_message_reply *reply = wr_bd->reply_bd;
		S5ClientIocb* io = vol->pick_iocb(reply->command_id, reply->command_seq);
		uint64_t ms1 = 1000;
		/*
		 * In io timeout case, we just ignore this completion
		 */
		if (unlikely(io == NULL))
		{
			S5LOG_WARN("Priori IO back but timeout!");
			conn->dec_ref();
			reply_pool.free(wr_bd);
			return;
		}
		io->reply_time = now_time_usec();
		message_status s = (message_status)reply->status;
		if (unlikely(s & (MSG_STATUS_REOPEN)))
		{
			S5LOG_WARN( "Get reopen from store %s status code:%x, req meta_ver:%d store meta_ver:%d",
				conn->connection_info.c_str(), s, io->cmd_bd->cmd_bd->meta_ver, reply->meta_ver);
			if (vol->meta_ver < reply->meta_ver)
			{
				S5LOG_WARN("client meta_ver is:%d, store meta_ver is:%d. reopen volume", vol->meta_ver, reply->meta_ver);
				vol->vol_proc->event_queue.post_event(EVT_REOPEN_VOLUME, 0, (void *)(now_time_usec()));
			}
			return;
		}

		{
			s5_message_head* io_cmd = io->cmd_bd->cmd_bd;
			//On client side, we rely on the io timeout mechnism to release time connection
			//Here we just release the io task
			if (unlikely(io_cmd->opcode == S5_OP_HEARTBEAT))
			{
				__sync_fetch_and_sub(&conn->inflying_heartbeat, 1);
				free_iocb(io);
				return;
			}

			void* arg = io->ulp_arg;
			ulp_io_handler h = io->ulp_handler;
			uint64_t io_end_time = now_time_usec();
			uint64_t io_elapse_time = (io_end_time - io->submit_time) / ms1;

			if (io_elapse_time > 2000)
			{
				S5LOG_WARN("SLOW IO, shard id:%d, command_id:%d, vol:%s, since submit:%dms since send:%dms",
						   io_cmd->slba >> SHARD_LBA_CNT_ORDER,
						   io_cmd->command_id,
						   vol->volume_name.c_str(),
						   io_elapse_time,
						   (io_end_time-io->sent_time)/ms1
					);
			}

			free_iocb(io);
			h(s, arg);
		}
    }
    else if(wr_bd->wr_op != TCP_WR_SEND)
    {
       S5LOG_ERROR("Unexpected completion, op:%d", wr_bd->wr_op);
    }
}


void s5_close_volume(S5ClientVolumeInfo* volume)
{
	S5LOG_INFO("close volume:%s", volume->volume_name.c_str());
	volume->state = VOLUME_CLOSED;

	pthread_cancel(volume->timeout_thread.native_handle());
	volume->timeout_thread.join();

	volume->vol_proc->event_queue.post_event(EVT_THREAD_EXIT, 0, NULL);
	pthread_join(volume->vol_proc->tid, NULL);

	volume->conn_pool->close_all();
	volume->vol_proc->stop();
	volume->iocb_pool.destroy();
	volume->cmd_pool.destroy();
	volume->data_pool.destroy();
	volume->reply_pool.destroy();

//	for(int i=0;i<volume->shards.size();i++)
//	{
//		delete volume->shards[i];
//	}
	delete volume;
}

static int reopen_volume(S5ClientVolumeInfo* volume)
{
	int rc = 0;
	S5LOG_INFO( "Reopening volume %s@%s, meta_ver:%d", volume->volume_name.c_str(),
		volume->snap_seq == -1 ? "HEAD" : volume->snap_name.c_str(), volume->meta_ver);

	volume->status = VOLUME_DISCONNECTED;
	volume->close();

	rc = volume->do_open();

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
	return rc;
}

int resend_io(S5ClientVolumeInfo* volume, S5ClientIocb* io)
{
	S5LOG_WARN("Requeue IO(cid:%d", io->cmd_bd->cmd_bd->command_id);
	__sync_fetch_and_add(&io->cmd_bd->cmd_bd->task_seq, 1);
	int rc = volume->vol_proc->event_queue.post_event(EVT_IO_REQ, 0, io);
	if (rc)
	S5LOG_ERROR("Failed to resend_io io, rc:%d", rc);
	return rc;
}


const char* show_ver()
{
	return (const char*)s5_lib_ver;
}

int S5VolumeEventProc::process_event(int event_type, int arg_i, void* arg_p)
{
	return volume->process_event(event_type, arg_i, arg_p);
}

int S5ClientVolumeInfo::process_event(int event_type, int arg_i, void* arg_p)
{
	switch (event_type)
	{
	case EVT_IO_REQ:
	{
		S5ClientIocb* io = (S5ClientIocb*)arg_p;
		BufferDescriptor* cmd_bd = io->cmd_bd;
		s5_message_head *io_cmd = io->cmd_bd->cmd_bd;

		int shard_index = io_cmd->slba >> SHARD_LBA_CNT_ORDER;
		struct S5Connection* conn = get_shard_conn(shard_index);
		BufferDescriptor* io_reply = reply_pool.alloc();
		if (conn == NULL || io_reply == NULL)
		{
			io->ulp_handler(-EIO, io->ulp_arg);
			S5LOG_ERROR("conn == NULL ,command id:%d task_sequence:%d, io_cmd :%d", io_cmd->command_id, io_cmd->task_seq, io_cmd->opcode);
			iocb_pool.free(io);
			break;
		}
		io_cmd->meta_ver = (uint16_t)meta_ver;

		int rc = conn->post_recv(io_reply);
		if (rc)
		{
			S5LOG_ERROR("Failed to post reply_recv");
			io->ulp_handler(-EIO, io->ulp_arg);
			break;
		}
		rc = conn->post_send(cmd_bd);
		if (rc)
		{
			S5LOG_ERROR("Failed to post_request_send");
			io->ulp_handler(-EIO, io->ulp_arg);
			break;
		}
		io->is_timeout = FALSE;
		io->conn = conn;
		io->sent_time = US2MS(now_time_usec());
		conn->add_ref();
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
		S5ClientIocb* io = (S5ClientIocb*)arg_p;
		S5LOG_WARN("volume_proc timeout, cid:%d, store:%s", io->cmd_bd->cmd_bd->command_id, io->conn->peer_ip.c_str());
		/*
		 * If time_recv is 0, io task:1)does not begin, 2)has finished.
		 */
		int64_t io_timeout_ms = io_timeout*1000;
		if (io->sent_time != 0 && US2MS(now_time_usec()) > io->sent_time + io_timeout_ms)
		{
			string conn_str = std::move(io->conn->connection_info);
			io->conn->close();
			io->conn->dec_ref();
			io->sent_time = 0;
			io->conn = NULL;
			s5_message_head *io_cmd = (s5_message_head *)io->cmd_bd->buf;
			if (unlikely(io_cmd->opcode == S5_OP_HEARTBEAT))
			{
				S5LOG_ERROR("heartbeat timeout for conn:%p", conn_str.c_str());
				iocb_pool.free(io);
				break;
			}
			S5LOG_WARN("IO(cid:%d) timeout, vol:%s, shard:%d, store:%s will reconnect and resend...",
				io_cmd->command_id, volume_name.c_str(), io_cmd->slba >> SHARD_LBA_CNT_ORDER, conn_str.c_str());
			int rc = resend_io(io);
			if(rc) {
				iocb_pool.free(io);
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
		if (iocb_pool.remain() == iocb_pool.obj_count)
			if (state == VOLUME_OPENED || state == VOLUME_REOPEN_FAIL || state == VOLUME_DISCONNECTED)
			{
				S5LOG_WARN("send volume reopen request for:%s", volume_name.c_str());
				conn_pool->close_all();
				vol_proc->event_queue.post_event(EVT_REOPEN_VOLUME, 0, (void*)(now_time_usec()));
			}
		break;
	}
	case EVT_SEND_HEARTBEAT:
	{
		struct buf_desc *comp;
		if (conn_pool->ip_id_map.size() == 0)
		{
			break;
		}
		next_heartbeat_idx %= conn_pool->ip_id_map.size();
		int hb_sent = 0;
		int ht_idx = 0;
		for (auto it = conn_pool->ip_id_map.begin(); it != conn_pool->ip_id_map.end();)
		{

			S5Connection* conn = it->second;
			if (conn->state != CONN_OK || __sync_fetch_and_sub(&conn->inflying_heartbeat, 0) > 2)
			{
				conn_pool->ip_id_map.erase(it++);
				S5LOG_ERROR("connection:%p:%s timeout", conn->connection_info.c_str());
				conn->close();
				conn->dec_ref();
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
			ht_idx ++;
		}
		break;
	}
	case EVT_THREAD_EXIT:
	{
		S5LOG_INFO("EVT_THREAD_EXIT received, exit now...");
		pthread_exit(0);
	}
	default:
		S5LOG_ERROR("Volume get unknown event:%d", event_type);
	}
	return 0;
}

/**
* get a shard connection from pool. connection is shared by shards on same node.
*/
S5Connection* S5ClientVolumeInfo::get_shard_conn(int shard_index)
{
	S5Connection* conn = NULL;
	if (state != VOLUME_OPENED)
	{
		return NULL;
	}
	S5ClientShardInfo * shard = &shards[shard_index];
	for (int i=0; i < shard->store_ips.size(); i++)
	{
		conn = conn_pool->get_conn(shard->store_ips[shard->current_ip]);
		if (conn != NULL) {
			return conn;
		}
		shard->current_ip = (shard->current_ip + 1) % shard->store_ips.size();
	}
	S5LOG_ERROR("Failed to get an usable IP for vol:%s shard:%d", volume_name.c_str(), shard_index);
	state = VOLUME_DISCONNECTED;
	return NULL;
}

void S5ClientVolumeInfo::timeout_check_proc()
{
	prctl(PR_SET_NAME, "clnt_time_chk");
	while (1)
	{
		if (sleep(CLIENT_TIMEOUT_CHECK_INTERVAL) != 0)
			return ;
		uint64_t now = now_time_usec();
		struct S5ClientIocb *ios = iocb_pool.data;
		int64_t io_timeout_us = io_timeout * 1000000LL;
		for (int i = 0; i < iocb_pool.obj_count; i++)
		{
			if (ios[i].sent_time != 0 && now > ios[i].sent_time + io_timeout_us && ios[i].is_timeout != 1)
			{
				S5LOG_DEBUG("IO timeout detected, cid:%d, volume:%s, timeout:%luus",
							((s5_message_head*)ios[i].cmd_bd->buf)->command_id,volume_name.c_str(), io_timeout_us);
				vol_proc->event_queue.post_event(EVT_IO_TIMEOUT, 0, &ios[i]);
				ios[i].is_timeout = 1;
			}
		}
	}

}