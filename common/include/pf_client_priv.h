#ifndef pf_client_priv_h__
#define pf_client_priv_h__

#include <thread>
#include "pf_mempool.h"
#include "pf_event_thread.h"
#include "pf_buffer.h"
#include "pf_client_api.h"

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


class PfClientVolumeInfo;
class PfConnectionPool;

class PfVolumeEventProc : public PfEventThread
{
public:
	PfVolumeEventProc(PfClientVolumeInfo* _volume) :volume(_volume) {};
	PfClientVolumeInfo* volume;
	virtual int process_event(int event_type, int arg_i, void* arg_p);
};

class PfClientIocb
{
public:
	BufferDescriptor* cmd_bd;
	BufferDescriptor* data_bd;
	BufferDescriptor* reply_bd; //Used by dispatcher tasks
	void* user_buf;			//used by qfa_client to store user buffer
	ulp_io_handler ulp_handler; //up layer protocol io handler
	void* ulp_arg;

	PfConnection *conn;
	BOOL is_timeout;

	uint64_t sent_time; //time the io sent to server
	uint64_t submit_time;//time the io was submitted by up layer
	uint64_t reply_time; // the time get reply from server
};

class PfClientShardInfo
{
public:

	//following are from server open_volume reply
	int index;
	std::vector<std::string> store_ips;

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

class PfClientVolumeInfo
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
	int state;
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
	int do_open();
	void close();
	int process_event(int event_type, int arg_i, void* arg_p);
	int resend_io(PfClientIocb* io);
	void timeout_check_proc();
	inline PfClientIocb* pick_iocb(uint16_t cid, uint16_t cmd_seq){
		//TODO: check cmd_seq
		return &iocb_pool.data[cid];
	}
	inline void free_iocb(PfClientIocb* io)	{
		iocb_pool.free(io);
	}

	PfConnection* get_shard_conn(int shard_index);
	void client_do_complete(int wc_status, BufferDescriptor* wr_bd);
};


#define SECT_SIZE_MASK (512-1) //sector size in linux is always 512 byte

#endif // pf_client_priv_h__

