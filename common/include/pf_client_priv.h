#ifndef pf_client_priv_h__
#define pf_client_priv_h__

#include <thread>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include "pf_mempool.h"
#include "pf_event_thread.h"
#include "pf_buffer.h"
#include "pf_client_api.h"
#define DEFAULT_HTTP_QUERY_INTERVAL 3

class PfPoller;

/*
 * for open volume, jconductor will return a json like:
 *  {
 *      "op":"open_volume_reply",
 *	    "status": "OK",
 *	    "meta_ver":0,
 *      "volume_name":"myvolname",
 *      "volume_size":10000000,
 *      "volume_id":12345678,
 *      "shard_count":2,
 *      "rep_count":3,
 *      "shards":[
 *               { "index":0, "store_ips":["192.168.3.1", "192.168.3.3"]
 * 			 },
 *               { "index":1, "store_ips":["192.168.3.1", "192.168.3.3"]
 * 			 }
 * 			]
 *   }
 */


class PfClientVolume;
class PfConnectionPool;

class PfVolumeEventProc : public PfEventThread
{
public:
	PfVolumeEventProc(PfClientVolume* _volume) : volume(_volume) {};
	PfClientVolume* volume;
	virtual int process_event(int event_type, int arg_i, void* arg_p);
};

class PfClientIocb
{
public:
	BufferDescriptor* cmd_bd;
	BufferDescriptor* data_bd;
	BufferDescriptor* reply_bd; //Used by dispatcher tasks
	union{
		void* user_buf;			//used by qfa_client to store user buffer
		const struct iovec* user_iov;
	};
	int user_iov_cnt;
	ulp_io_handler ulp_handler; //up layer protocol io handler
	void* ulp_arg;

	PfConnection *conn;
	BOOL is_timeout;

	uint64_t sent_time; //time the io sent to server
	uint64_t submit_time;//time the io was submitted by up layer
	uint64_t reply_time; // the time get reply from server

	PfClientIocb* list_next;
	PfClientIocb* list_prev;
};
template<typename T>
class PfDoublyList{
public:
	T head;
	PfDoublyList() {
		head->list_next = &head;
		head->list_prev = &head;
	}
	inline __attribute__((always_inline)) void append(T* element) {
		element->list_next = head->list_next;
		element->list_prev = head;
		head->list_next->prev = element;
		head->list_next = element;
	}

	inline __attribute__((always_inline)) void remove(T* element) {
		element->list_next->list_prev = element->list_prev;
		element->list_prev->list_next = element->list_next;
	}

	inline __attribute__((always_inline)) T* pop() {
		T* e = head->list_next;
		if(e == &head)
			return NULL;
		remove(e);
		return e;
	}
};


class PfClientShardInfo
{
public:

	//following are from server open_volume reply
	int index;
	std::string store_ips; //a comma separated string from jconductor
	std::string status;
	std::vector<std::string> parsed_store_ips; //splited from store_ips

	//following are internal data constructed by client
	std::vector<int> conn_id; // connection ID in connection pool. this vector correspond to store_ips
	int current_ip;
	PfClientShardInfo() :current_ip(0) {};
};
enum PfVolumeState
{
	VOLUME_CLOSED = 0,
	VOLUME_OPENED = 1,
	VOLUME_WAIT_REOPEN = 2,
	VOLUME_REOPEN_FAIL = 3,
	VOLUME_DISCONNECTED = 4
};

class PfClientVolume
{
public:
	//following data are from server open_volume reply
	std::string status;
	std::string volume_name;
	std::string snap_name;

	uint64_t volume_size;
	uint64_t volume_id;
	int shard_count;
	int rep_count;
	int meta_ver;
	int snap_seq;
	std::vector<PfClientShardInfo> shards;

	//following are internal data constructed by client
	PfConnectionPool* conn_pool;
	std::string cfg_file;
	int io_depth;
	int io_timeout; //timeout in second
	PfVolumeState state;
	PfEventQueue* event_queue;
	ObjectMemoryPool<PfClientIocb> iocb_pool;
	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
	int shard_lba_cnt_order; //to support variable shard size. https://github.com/cocalele/PureFlash/projects/1#card-32329729

	PfVolumeEventProc *vol_proc;
	std::thread timeout_thread;

	int next_heartbeat_idx;

	PfPoller* tcp_poller;
	uint64_t open_time; //opened time, in us, returned by now_time_usec()
public:
	int do_open(bool reopen=false, bool is_aof=false);
	void close();
	int process_event(int event_type, int arg_i, void* arg_p);
	int resend_io(PfClientIocb* io);
	void timeout_check_proc();
	inline PfClientIocb* pick_iocb(uint16_t cid, uint32_t cmd_seq){
		//TODO: check cmd_seq
		return &iocb_pool.data[cid];
	}
	inline void free_iocb(PfClientIocb* io)	{
		iocb_pool.free(io);
	}

	PfConnection* get_shard_conn(int shard_index);
	void client_do_complete(int wc_status, BufferDescriptor* wr_bd);
};

class ListVolumeReply
{
public:
	std::vector<PfClientVolumeInfo> volumes;
	int ret_code;
	std::string reason;
};

class GeneralReply
{
public:
	std::string op;
	int ret_code;
	std::string reason;
};
void from_json(const nlohmann::json& j, GeneralReply& reply);

#define SECT_SIZE_MASK (512-1) //sector size in linux is always 512 byte

void* pf_http_get(std::string& url, int timeout_sec, int retry_times);
std::string get_master_conductor_ip(const char *zk_host, const char* cluster_name);

template<typename ReplyT>
int query_conductor(conf_file_t cfg, const std::string& query_str, ReplyT& reply, bool no_exception=false)
{
	const char* zk_ip = conf_get(cfg, "zookeeper", "ip", "", TRUE);
	if(zk_ip == NULL)
	{
		throw std::runtime_error("zookeeper ip not found in conf file");
	}
	const char* cluster_name = conf_get(cfg, "cluster", "name", "cluster1", FALSE);
	int open_volume_timeout = conf_get_int(cfg, "client", "open_volume_timeout", 30, FALSE);

	int retry_times = 5;
	for (int i = 0; i < retry_times; i++)
	{
		std::string conductor_ip = get_master_conductor_ip(zk_ip, cluster_name);
		std::string url = format_string( "http://%s:49180/s5c/?%s", conductor_ip.c_str(), query_str.c_str());
		void* reply_buf = pf_http_get(url, open_volume_timeout, 1);
		if( reply_buf != NULL) {
			DeferCall _rel([reply_buf]() { free(reply_buf); });
			auto j = nlohmann::json::parse((char*)reply_buf);
			if (j["ret_code"].get<int>() != 0 && !no_exception) {
				throw std::runtime_error(format_string("Failed %s, reason:%s", url.c_str(), j["reason"].get<std::string>().c_str()));
			}
			j.get_to<ReplyT>(reply);
			return 0;
		}
		if (i < retry_times - 1)
		{
			S5LOG_ERROR("Failed query %s, will retry", url.c_str());
			::sleep(DEFAULT_HTTP_QUERY_INTERVAL);
		}
	}

	return -1;
}

#endif // pf_client_priv_h__

