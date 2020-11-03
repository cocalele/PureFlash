#ifndef pf_replicator_h__
#define pf_replicator_h__
#include <unordered_map>

#include "pf_poller.h"
#include "pf_connection_pool.h"
#include "pf_client_priv.h"

class RecoverySubTask;

class PfReplicator : public PfEventThread
{
	class PeerAddr
	{
	public:
		int store_id;
		PfConnection *conn;
		int curr_ip_idx;
		std::vector<std::string> ip;
	};
	class PfRepConnectionPool : public PfConnectionPool
	{
	public:
		std::unordered_map<int, PeerAddr> peers;
		void add_peer(int store_id, std::string ip1, std::string ip2);
		void connect_peer(int store_id);
		PfConnection* get_conn(int store_id);
	};

public:
	int init(int index);
	int process_event(int event_type, int arg_i, void* arg_p);
	int begin_replicate_io(IoSubTask* t);
	int begin_recovery_read_io(RecoverySubTask* t);
	inline PfClientIocb* pick_iocb(uint16_t cid, uint32_t cmd_seq){
		//TODO: check cmd_seq
		return &iocb_pool.data[cid];
	}
	int process_io_complete(PfClientIocb* io, int complete_status);

	int rep_index;

	PfPoller *tcp_poller;
	PfRepConnectionPool *conn_pool;
	ObjectMemoryPool<PfClientIocb> iocb_pool;
	BufferPool cmd_pool;
	BufferPool reply_pool;
};
#endif // pf_replicator_h__
