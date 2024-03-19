/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef pf_replicator_h__
#define pf_replicator_h__
#include <unordered_map>

#include "pf_poller.h"
#include "pf_connection_pool.h"
#include "pf_client_priv.h"

class RecoverySubTask;
class PfDelayThread : public PfEventThread
{

public:
	//PfDelayThread(PfReplicator* r) { replicator = r; }
	PfReplicator* replicator;
	int process_event(int event_type, int arg_i, void* arg_p, void* arg_q);
};

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
	int init(int index, uint16_t* p_id);
	int process_event(int event_type, int arg_i, void* arg_p, void* arg_q);
	int begin_replicate_io(IoSubTask* t);
	int begin_recovery_read_io(RecoverySubTask* t);
	inline PfClientIocb* pick_iocb(uint16_t cid, uint32_t cmd_seq){
		//TODO: check cmd_seq
		return &iocb_pool.data[cid];
	}
	int process_io_complete(PfClientIocb* iocb, int _complete_status);
	int handle_conn_close(PfConnection* c);

	int rep_index;

	PfPoller *tcp_poller;
	//PfPoller *rdma_poller;
	PfRepConnectionPool *conn_pool;
	ObjectMemoryPool<PfClientIocb> iocb_pool;
	struct replicator_mem_pool mem_pool;

	PfDelayThread delay_thread;
};
#endif // pf_replicator_h__
