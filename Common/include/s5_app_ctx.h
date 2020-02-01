#ifndef s5_app_ctx_h__
#define s5_app_ctx_h__
#include <string>

#include "s5_buffer.h"
#include "s5_mempool.h"

#define MAX_IO_DEPTH 128
#define MAX_IO_SIZE (64<<10) //max IO

class BufferDescriptor;
class S5IoDesc
{
public:
	BufferDescriptor* io_cmd;
	BufferDescriptor* io_data;
	BufferDescriptor* io_reply;
};
class S5AppCtx
{
public:
	int io_desc_count;
	ObjectMemoryPool<S5IoDesc> iod_pool;

	//BufferPool cmd_pool;
	//BufferPool data_pool;
	//BufferPool reply_pool;
	//BufferPool handshake_pool;

	//ObjectMemoryPool<s5_message_head> cmd_pool;
	//ObjectMemoryPool<byte[64 << 10]> data_pool;
	//ObjectMemoryPool<s5_io_reply> reply_pool;
	//ObjectMemoryPool<s5_handshake_msg> handshake_pool;

	std::string conf_file_name;
	conf_file_t conf;
};

extern S5AppCtx* g_app_ctx;
#endif // s5_app_ctx_h__
