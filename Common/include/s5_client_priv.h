#ifndef s5_client_priv_h__
#define s5_client_priv_h__

#include <thread>
#include "s5_mempool.h"
#include "s5_event_thread.h"
#include "s5_buffer.h"

/*
 * for open volume, jconductor will return a json like:
 *  {
 *      "op":"open_volume_reply",
 *	    "status": "OK",
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
typedef void(*ulp_io_handler)(int complete_status, void* cbk_arg);

class S5ConnectionPool;
class S5ClientVolumeInfo;
class S5VolumeEventProc : public S5EventThread
{
public:
	S5VolumeEventProc(S5ClientVolumeInfo* _volume) :volume(_volume) {};
	S5ClientVolumeInfo* volume;
	virtual int process_event(int event_type, int arg_i, void* arg_p);
};

class S5ClientIocb
{
public:
	BufferDescriptor* cmd_bd;
	BufferDescriptor* data_bd;
	BufferDescriptor* reply_bd; //Used by dispatcher tasks
	void* user_buf;			//used by qfa_client to store user buffer
	ulp_io_handler ulp_handler; //up layer protocol io handler
	void* ulp_arg;

	S5Connection *conn;
	BOOL is_timeout;

	uint64_t sent_time; //time the io sent to server
	uint64_t submit_time;//time the io was submitted by up layer
	uint64_t reply_time; // the time get reply from server
};

class S5ClientShardInfo
{
public:

	//following are from server open_volume reply
	int index;
	std::vector<std::string> store_ips;

	//following are internal data constructed by client
	std::vector<int> conn_id; // connection ID in connection pool. this vector correspond to store_ips
	int current_ip;
	S5ClientShardInfo() :current_ip(0) {};
};
enum S5VolumeState
{
	VOLUME_CLOSED = 0,
	VOLUME_OPENED = 1,
	VOLUME_WAIT_REOPEN = 2,
	VOLUME_REOPEN_FAIL = 3,
	VOLUME_DISCONNECTED = 4
};

class S5ClientVolumeInfo
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
	std::vector<S5ClientShardInfo> shards;

	//following are internal data constructed by client
	S5ConnectionPool* conn_pool;
	std::string cfg_file;
	int io_depth;
	int io_timeout; //timeout in second
	int state;
	ObjectMemoryPool<S5ClientIocb> iocb_pool;
	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
	int shard_lba_cnt_order; //to support variable shard size. https://github.com/cocalele/PureFlash/projects/1#card-32329729

	S5VolumeEventProc *vol_proc;
	std::thread timeout_thread;

	int next_heartbeat_idx;

	S5Poller* tcp_poller;
	uint64_t open_time; //opened time, in us, returned by now_time_usec()
public:
	int do_open();
	void close();
	int process_event(int event_type, int arg_i, void* arg_p);
	int resend_io(S5ClientIocb* io);
	void timeout_check_proc();
	S5ClientIocb* pick_iocb(uint16_t cid, uint16_t cmd_seq);
	void free_iocb(S5ClientIocb* io);
	S5Connection* get_shard_conn(int shard_index);
	void client_do_complete(int wc_status, BufferDescriptor* wr_bd);
};




#endif // s5_client_priv_h__

