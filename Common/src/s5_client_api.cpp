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

#define S5_LIB_VER 0x00010000
static char* s5_lib_ver = "S5 client version:0x00010000";

using namespace std;

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
		_clean.push_back([]() { delete volume; });
		//other calls
		volume->volume_name = volume_name;
		volume->cfg_file = cfg_filename;
		volume->snap_name = snap_name;

		rc = volume->do_open();
		if (rc)	{
			return NULL;
		}


		rc = pthread_create(&volume->timeout_proc_tid, NULL, client_timeout_check_proc, volume);
		goto_if(release7d, rc != 0, "Failed to create timeout check thread");
		rc = pthread_create(&volume->vol_proc_tid, NULL, volume_proc, volume);
		goto_if(release8, rc != 0, "Failed to create volume worker thread");
		volume->state = VOLUME_OPENED;
		volume->connected = TRUE;
		rc = pthread_create(&volume->vol_monitor_tid, NULL, client_volume_monitor, volume);
		goto_if(release9, rc != 0, "Failed to create volume monitor thread");

		qfa_log(NEON_LOG_INFO, "Succeeded open volume %s@%s(0x%lx), meta_ver=%d, io_depth=%d", volume->name,
			volume->snap_seq == -1 ? "HEAD" : volume->snap_name, volume->id, volume->meta_ver, volume->max_io_depth);
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
	conn->volume->post_event(&vol->task_receiver, EVT_IO_COMPLETE, status, completion_ctx);
}


int S5ClientVolumeInfo::do_open()
{
	int rc = 0;
	conf_file_t cfg = conf_open(cfg_file.c_str());
	if(cfg == NULL)
	{
		return -errno;
	}
	DeferCall _cfg_r([]() { conf_close(cfg); });
	io_depth = conf_get_int(cfg, "client", "io_depth", 32, FALSE);
	io_timeout = conf_get_int(cfg, "client", "io_timeout", 30, FALSE);

	char* esc_vol_name = curl_easy_escape(NULL, volume_name.c_str(), 0);
	if (!esc_vol_name)
	{
		throw runtime_error("Curl easy escape failed.\n");
	}
	DeferCall _1([]() { curl_free(esc_vol_name); });
	char* esc_snap_name = curl_easy_escape(NULL, snap_name.c_str(), 0);
	if (!esc_snap_name)
	{
		throw runtime_error("Curl easy escape failed.\n");
	}
	DeferCall _2([]() { curl_free(esc_snap_name); });

	std::string query = format_string("/op=open_volume&volume_name=%s&snap_name=%s", esc_vol_name, esc_snap_name);
	rc = query_conductor(cfg, query, *this);
	if (rc != 0)
		return rc;

	conn_pool = new S5ConnectionPool();
	if (conn_pool == NULL)
		throw bad_alloc("No memory to alloc memory pool");
	data_pool.init(S5_MAX_IO_SIZE, io_depth, shards.size()*2, client_on_work_complete);
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

static string get_master_conductor_ip(char *zk_host)
{
    struct String_vector condutors = {0};
    char **str = NULL;
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zhandle_t *zkhandle = zookeeper_init(zk_host, NULL, ZK_TIMEOUT, NULL, NULL, 0);
    if (zkhandle == NULL)
    {
        throw std::runtime_error("Error zookeeper_init");
    }
	DeferCall _r_z([]() { zookeeper_close(zkhandle); });

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
	DeferCall _r_c([]() {deallocate_String_vector(&condutors); });
    str = (char **)condutors.data;
    qsort(str, condutors.count, sizeof(char *), cmp);

	char leader_path[256];
    int len = snprintf(leader_path, sizeof(leader_path), "%s/%s", zk_root, str[0]);
    if (len >= sizeof(leader_path) || len < 0)
    {
		throw std::runtime_error("Cluster name is too long, max length is:%d", sizeof(leader_path));
    }

	string ip_str(256);
    if (ZOK != zoo_get(zkhandle, leader_path, 0, ip_str.data(), ip_str.capacity(), 0))
    {
		throw std::runtime_error("Error when get S5 conductor leader data...");
    }
    S5LOG_INFO("Get S5 conductor IP:%s", ip_str.c_str());
    return ip_str;
}

static size_t write_mem_callback(void *contents, size_t size, size_t nmemb, void *buf)
{
	size_t realsize = size * nmemb;
	curl_memory_t *mem = (curl_memory_t *)buf;

	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL)
	{
		S5LOG_ERROR("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = '\0';

	return realsize;
}

template<typename ReplyT>
static int query_conductor(conf_file_t cfg, const string& query_str, ReplyT& reply)
{
	char zk_servers[1024] = { 0 };

	char* zk_ip = conf_get(cfg, "zookeeper", "ip", "", TRUE);
	if(zk_ip == NULL || strlen(zk_servers) == 0)
    {
		throw std::runtime_error("zookeeper ip not found in conf file");
    }


	CURLcode res;
	CURL *curl = NULL;
	curl_memory_t curl_buf;
	curl_buf.memory = (char *)malloc(1 << 20);
	if (curl_buf.memory == NULL)
		throw std::bad_alloc("Failed alloc curl buffer");
	curl_buf.size = 0;
	DeferCall _fb([]() {free(curl_buf.memory); });
	int open_volume_timeout = conf_get_int(cfg, "client", "open_volume_timeout", 30, FALSE);

	curl = curl_easy_init();
	if (curl == NULL)
	{
		throw std::runtime_error("curl_easy_init failed.");
	}
	DeferCall _r_curl([]() { curl_easy_cleanup(curl); });

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&curl_buf);
	//curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// Set timeout.
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, open_volume_timeout);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
	char qfc_ip[32] = { 0 };
	char url[MAX_URL_LEN] = { 0 };


	for (int i = 0; i < retry_times; i++)
	{
		/* Query qfcenter active node from zk. */
		string conductor_ip = get_master_conductor_ip(zk_ip);

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



void client_do_complete(int wc_status, struct buf_desc* wr_bd)
{
	if (unlikely(wc_status != TCP_WC_SUCCESS))
	{
		qfa_log(NEON_LOG_INFO, "Op complete unsuccessful opcode:%d, status:%d", wr_bd->opcode, wc_status);
		if (wr_bd->opcode == TCP_WC_RECV)
		{
			put_back_fail_completion(wr_bd);
		}
		return;
	}

 //   qfa_log(NEON_LOG_INFO, "client_do_complete opcode:%d", wr_bd->opcode);
    if (wr_bd->opcode == TCP_WC_RECV)
    {
//		qfa_log(NEON_LOG_INFO, "succeed of TCP_WC_RECV");
		struct qfa_client_volume_priv* vol = wr_bd->vol;
		struct qfa_completion *comp = wr_bd->io_comp;
		struct io_task* io = qfa_pick_io_task(&vol->iotask_pool, comp->command_id, comp->task_sequence);
		uint64_t ms1 = 1000000;
		/*
		 * In io timeout case, we just ignore this completion
		 */
		if (unlikely(io == NULL))
		{
			qfa_log(NEON_LOG_INFO, "Priori IO back and timeout, ignored");
			put_back_fail_completion(wr_bd);
			return;
		}
		io->time_reply = now_time_nsec();
		__le16 s = comp->status;
		if (unlikely(s & (QFA_SC_REOPEN|QFA_SC_RETRY)))
		{
			int io_handled = 0;
			if (s&QFA_SC_REOPEN)
			{
				//qfa_log(NEON_LOG_WARN, "client_do_complete Get reopen from store %s status code:%x", inet_ntoa(wr_bd->conn->peer_addr.sin_addr), s);
				qfa_log(NEON_LOG_WARN, "Get reopen from store %s status code:%x, req meta_ver:%d store meta_ver:%d",
					inet_ntoa(io->conn->peer_addr.sin_addr), s, io->cmd_bd->io_cmd->meta_ver, comp->meta_ver);
				if (vol->meta_ver < comp->meta_ver) //If volume's version is already latest, do not reopen volume
				{
					qfa_log(NEON_LOG_WARN, "client meta_ver is:%d, store meta_ver is:%d. reopen volume", vol->meta_ver, comp->meta_ver);
					qfa_post_event(&vol->task_receiver, EVT_REOPEN_VOLUME, 0, (void *)(now_time_nsec()));
				}
			}
			if (s&QFA_SC_RETRY)
			{
				io_handled = 1;
				conn_dec_ref(io->conn);
				qfa_free_comp(wr_bd);
				int rc = resend_io(vol, io);
				if (rc)
				{
					qfa_log(NEON_LOG_ERROR, "Failed resend io");
					io_handled = 0;
				}
			}
			if (io_handled)
				return;
		}

		{
			struct buf_desc* request_ctx = io->cmd_bd;
			//On client side, we rely on the io timeout mechnism to release time connection
			//Here we just release the io task
			if (request_ctx->io_cmd->opcode == qfa_op_heartbeat)
			{
				__sync_fetch_and_sub(&io->conn->heartbeat_send_receive_diff, 1);
				conn_dec_ref(io->conn);
				qfa_free_comp(wr_bd);
				qfa_free_task(io);
				//qfa_log(NEON_LOG_DEBUG, "get heartbeat response from server.:%p", io->cmd_bd->conn);
				return;
			}
			io->time_srv = comp->srv_time;
			io->client_to_server_trans = comp->client_to_server_trans;
			io->server_to_client_trans = now_time_nsec() - comp->server_send_time;
			if (vol->transport == RDMA)
			{
				if (request_ctx->io_cmd->opcode == qfa_op_read)
				{
					memcpy(io->user_buf, io->data_bd->buf, request_ctx->io_cmd->nlba << LBA_SIZE_ORDER);
				}
				qfa_free_bd(&vol->data_bd_pool, io->data_bd);
			}
			void* arg = io->ulp_arg;
			ulp_io_handler h = io->ulp_handler;
			uint64_t io_end_time = io->time_comp = now_time_nsec();
			uint64_t io_elapse_time = (io_end_time - io->time_recv) / ms1;
			add_io_elapse_time(vol, 15, io_elapse_time);

			if (io->time_comp - io->time_submit > 2000*ms1)
			{
				qfa_log(NEON_LOG_WARN, "SLOW IO, shard id:%d, command_id:%d, vol:%s, send-submit:%dms reply-send:%dms comp-reply:%dms, srv_time:%dus",
						io->cmd_bd->io_cmd->slba >> SHARD_LBA_CNT_ORDER,
						io->cmd_bd->io_cmd->command_id,
						vol->name,
					(io->time_recv - io->time_submit)/ms1,
					(io->time_reply - io->time_recv)/ms1,
					(io->time_comp - io->time_reply)/ms1,
					io->time_srv/1000);
			}

			conn_dec_ref(io->conn);
			qfa_free_comp(wr_bd);
			qfa_free_task(io);
			h(s, arg);
		}
    }
    else if(wr_bd->opcode != TCP_WC_SEND)
    {
        qfa_log(NEON_LOG_ERROR, "Unexpected completion, op:%d", wr_bd->opcode);
    }
}


void qfa_close_volume(struct qfa_client_volume* volume_)
{
	S5LOG_INFO("close volume:%s", volume->name);
	volume->state = VOLUME_CLOSED;

	pthread_cancel(volume->ratelimit_proc_tid);
	pthread_join(volume->ratelimit_proc_tid, NULL);
	flowctrl_destroy(&volume->flowctrl);

	pthread_cancel(volume->vol_monitor_tid);
	pthread_join(volume->vol_monitor_tid, NULL);
	pthread_cancel(volume->timeout_proc_tid);
	pthread_join(volume->timeout_proc_tid, NULL);
	if (volume->transport == RDMA)
	{
#ifndef NO_RDMA
		struct qfa_srq_pool *srq_pool = &volume->vol_srq_pool;
	//1.disconnect qp to flush wrs
		qfa_release_all_client_qp(&volume->conn_pool);
	//2.destroy SRQ after reclaiming all wrs
		qfa_destroy_rdma_srq(srq_pool);
#endif
	}
	qfa_post_event(&volume->task_receiver, EVT_THREAD_EXIT, 0, NULL);
	pthread_join(volume->vol_proc_tid, NULL);
	if (volume->transport == RDMA)
	{
#ifndef NO_RDMA
		struct qfa_srq_pool *srq_pool = &volume->vol_srq_pool;
		//3.release SRQ related source
		qfa_release_srq_pool(srq_pool);
#endif
	}
	for (int i = 0; i < CB_NUM; i ++) {
		if (volume->callback_table[i] != NULL) {
			struct callback_record *cbrec = (struct callback_record *)volume->callback_table[i];
			struct resize_cb_arg *recbarg = (struct resize_cb_arg *)cbrec->neon_cb_arg;
			if (recbarg != NULL) {
				free(recbarg);
			}
			free(cbrec);
		}
	}
	qfa_release_conn_pool(&volume->conn_pool);
	qfa_release_event_queue(&volume->task_receiver);
	qfa_release_task_pool(&volume->iotask_pool, 0);
	release_buf_desc_pool(&volume->data_bd_pool);
	release_config(volume->cfg_file);
	free(volume->shards);
	free(volume);
}

static int reopen_volume(struct qfa_client_volume_priv* volume)
{
	int rc = 0;
	qfa_log(NEON_LOG_INFO, "Reopening volume %s@%s, meta_ver:%d", volume->name,
		volume->snap_seq == -1 ? "HEAD" : volume->snap_name, volume->meta_ver);
	if (volume->shards)
	{
		free(volume->shards);
	}
	volume->shards = NULL;
	/*
	 * Eg: When reopen_volume is triggered by store, not by get_shard_conn,
	 * there should be a place to set volume connect status as FALSE to prevent
	 * following IO calling get_shard_conn when volume connected is TRUE but in fact
	 * the volume has been disconnected.
	 */
	volume->connected = FALSE;

	rc = open_volume_common(&volume->volume_ops, NEONSAN_LIB_VER);
	if (unlikely(rc))
	{
		qfa_log(NEON_LOG_ERROR, "Failed reopen volume!");
		volume->state = VOLUME_REOPEN_FAIL;
		return rc;
	} else {
		if (unlikely(volume->size != volume->size_prev)) {
			qfa_log(NEON_LOG_DEBUG, "volume size change from %ld to %ld", volume->size_prev, volume->size);
			struct callback_record *cbrec = volume->callback_table[CB_RESIZE];
			if (cbrec != NULL) {
				struct resize_cb_arg *neon_cb_arg = (struct resize_cb_arg*)cbrec->neon_cb_arg;
				neon_cb_arg->newsize = volume->size;
				qfa_log(NEON_LOG_DEBUG, "neon_cb_arg newsize:%ld", neon_cb_arg->newsize);
				cb_func_type ulp_cb = (cb_func_type)cbrec->func;
				rc = ulp_cb(cbrec->neon_cb_arg);
				//need to check
				if (rc != 0) {
					qfa_log(NEON_LOG_DEBUG, "neon resize callback fail:%d", rc);
				}
			} else {
				qfa_log(NEON_LOG_DEBUG, "volume size change, no callback fuc to notify ulp, new size:%ld", volume->size);
			}
			volume->size_prev = volume->size;
		}
	}
	volume->state = VOLUME_OPENED;
	volume->connected = TRUE;
	qfa_log(NEON_LOG_INFO, "Succeeded reopen volume %s@%s(0x%lx), meta_ver:%d io_depth=%d", volume->name,
		volume->snap_seq == -1 ? "HEAD" : volume->snap_name, volume->id, volume->meta_ver, volume->max_io_depth);
	return rc;
}


static void* client_timeout_check_proc(void* _v)
{
	prctl(PR_SET_NAME, "clnt_time_chk");
	struct qfa_client_volume_priv* v = (struct qfa_client_volume_priv*)_v;
	while (1)
	{
		if (sleep(CLIENT_TIMEOUT_CHECK_INTERVAL) != 0)
			return NULL;
		uint64_t now = now_time_nsec();
		struct io_task *tasks = v->iotask_pool.tasks;
        int64_t io_timeout_ns = SEC_TO_NSEC(v->io_timeout);
		for (int i = 0; i < v->iotask_pool.block_count; i++)
		{
			if (tasks[i].time_recv != 0 && now > tasks[i].time_recv + io_timeout_ns && tasks[i].is_timeout != 1)
			{
				qfa_log(NEON_LOG_DEBUG, "IO timeout detected, cid:%d, volume:%s, timeoutinterval:%lu", tasks[i].cmd_bd->io_cmd->command_id,v->name, io_timeout_ns);
				qfa_post_event(&v->task_receiver, EVT_IO_TIMEOUT, 0, &tasks[i]);
				tasks[i].is_timeout = 1;
			}
		}
	}
	return NULL;
}



int resend_io(struct qfa_client_volume_priv* volume, struct io_task* io)
{
	qfa_log(NEON_LOG_WARN, "Requeue IO(cid:%d", io->cmd_bd->io_cmd->command_id);
	__sync_fetch_and_add(&io->cmd_bd->io_cmd->task_seq, 1);
	int rc = qfa_post_event(&volume->task_receiver, EVT_IO_REQ, 0, io);
	if (rc)
		qfa_log(NEON_LOG_ERROR, "Failed to resend_io io, rc:%d", rc);
	return rc;
}


const char* show_ver()
{
	// printf("%s\n", neon_lib_ver);
	return (const char*)s5_lib_ver;
}

int S5VolumeEventProc::process_event(int event_type, int arg_i, void* arg_p)
{
	return volume->vol_proc->post_event(event_type, arg_i, arg_p);
}

int S5ClientVolumeInfo::process_event(int event_type, int arg_i, void* arg_p)
{
	switch (event_type)
	{
	case EVT_IO_REQ:
	{
		S5ClientIocb* io = (struct io_task*)evt.arg_p;
		s5_message_head *io_cmd = (s5_message_head *)io->cmd_bd->buf;

		int shard_index = io_cmd->slba >> SHARD_LBA_CNT_ORDER;
		struct S5Connection* conn = get_shard_conn(shard_index);
		s5_message_reply* io_reply = reply_pool->alloc();
		if (conn == NULL || io_reply == NULL)
		{
			io->ulp_handler(-EIO, io->ulp_arg);
			S5LOG_ERROR("conn == NULL ,command id:%d task_sequence:%d, io_cmd :%d", io_cmd->command_id, io_cmd->task_seq, io_cmd->opcode);
			iocb_pool->free(io);
			break;
		}
		io_cmd->meta_ver = volume->meta_ver;

		int rc = conn->post_recv(io_reply);
		if (rc)
		{
			S5LOG_ERROR("Failed to post reply_recv");
			io->ulp_handler(-EIO, io->ulp_arg);
			break;
		}
		rc = conn->post_send(io_cmd);
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
		client_do_complete(evt.arg_i, (struct buf_desc*)evt.arg_p);
		break;
	case EVT_IO_TIMEOUT:
	{
		if (state != VOLUME_OPENED)
		{
			S5LOG_WARN("volume state is:%d", state);
			break;
		}
		S5ClientIocb* io = (S5ClientIocb*)evt.arg_p;
		S5LOG_WARN("volume_proc timeout, cid:%d, store:%s", io->cmd_bd->io_cmd->command_id, inet_ntoa(io->conn->peer_addr.sin_addr));
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
			if (unlikely(io_cmd->opcode == MSG_TYPE_HEARTBEAT))
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
		if ((uint64_t)evt.arg_p > opened_time)
		{
			reopen_volume(v);
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
				main_proc->post_event(EVT_REOPEN_VOLUME, 0, (void*)(now_time_usec()));
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
		int ht_idx = 0;
		int hb_sent = 0;

		while (auto it = conn_pool->ip_id_map.begin(); it != ip_id_map.end(); ++ht_idx)
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
		}
		break;
	}
	case EVT_THREAD_EXIT:
	{
		S5LOG_INFO("EVT_THREAD_EXIT received, exit now...");
		pthread_exit(0);
	}
	default:
		S5LOG_ERROR("Volume get unknown event:%d", evt.type);
	}
	return 0;
}

/**
* get a shard connection from pool. connection is shared by shards on same node.
*/
S5Connection* S5ClientVolumeInfo::get_shard_conn(int shard_index)
{
	struct qfa_connection* conn = NULL;
	if (state != VOLUME_OPENED)
	{
		return NULL;
	}
	S5ClientShardInfo * shard = &shards[shard_index];
	for (int i=0; i < shard->store_ips.size(); i++)
	{
		struct sockaddr_in* addr = &shard->store_ips[shard->current_ip];
		conn = conn_pool->get_conn(shard->store_ips[shard->current_ip]);
		if (conn != NULL) {
			return conn;
		}
		shard->current_ip = (shard->current_ip + 1) % shard->store_ips.size();
	}
	S5LOG_ERROR("Failed to get an usable IP for vol:%s shard:%d", name, shard_index);
	state = VOLUME_DISCONNECTED;
	return NULL;
}
