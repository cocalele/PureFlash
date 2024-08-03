#ifndef pf_app_ctx_h__
#define pf_app_ctx_h__
#include <string>

#include "pf_buffer.h"
#include "pf_mempool.h"
#include "pf_volume_type.h"
#include "pf_client_api.h"

#include "pf_rdma_connection.h"


enum {
	AIO,
	IO_URING,
	SPDK,
};

enum RDMA_CQ_PROC_MODEL {
	EVENT,
	POLLING,
	NONE_MODEL,
};

class BufferDescriptor;
class PfIoDesc
{
public:
	BufferDescriptor* io_cmd;
	BufferDescriptor* io_data;
	BufferDescriptor* io_reply;
};
class PfAppCtx
{
public:
	int io_desc_count;
	BigMemPool cow_buf_pool;

	//BufferPool cmd_pool;
	//BufferPool data_pool;
	//BufferPool reply_pool;
	//BufferPool handshake_pool;

	//ObjectMemoryPool<PfMessageHead> cmd_pool;
	//ObjectMemoryPool<byte[64 << 10]> data_pool;
	//ObjectMemoryPool<pf_io_reply> reply_pool;
	//ObjectMemoryPool<pf_handshake_msg> handshake_pool;

	std::string conf_file_name;
	conf_file_t conf;
	int engine;
	RDMA_CQ_PROC_MODEL cq_proc_model;
	bool shard_to_replicator = false;
	struct PfRdmaDevContext *dev_ctx[MAX_RDMA_DEVICE];
	virtual int PfRdmaRegisterMr(struct PfRdmaDevContext *dev_ctx) = 0 ;
	virtual void PfRdmaUnRegisterMr() = 0;
	bool rdma_client_only;
	PfAppCtx():cow_buf_pool(COW_OBJ_SIZE), engine(AIO), cq_proc_model(EVENT), shard_to_replicator(false)
	{
		for (int i = 0 ; i < MAX_RDMA_DEVICE; i++)
			dev_ctx[i] = NULL;

		rdma_client_only = false;
	}
	virtual ~PfAppCtx(){}
};

extern PfAppCtx* g_app_ctx;
extern bool spdk_engine;
static inline __attribute__((always_inline)) bool spdk_engine_used()
{
	return spdk_engine == true;
}
void spdk_engine_set(bool use_spdk);
#endif // pf_app_ctx_h__
