#ifndef s5_app_ctx_h__
#define s5_app_ctx_h__
class S5IoDesc
{
public:
	BufferDescriptor* io_cmd;
	BufferDescriptor* io_data;
	BufferDescriptor* io_reply;
};
class S5AppCtx
{
	int io_desc_count;
	ObjectMemoryPool<S5IoDesc> iod_pool;

	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
	BufferPool handshake_pool;

	//ObjectMemoryPool<s5_message_head> cmd_pool;
	//ObjectMemoryPool<byte[64 << 10]> data_pool;
	//ObjectMemoryPool<s5_io_reply> reply_pool;
	//ObjectMemoryPool<s5_handshake_msg> handshake_pool;

	conf_file_t conf;
};

#endif // s5_app_ctx_h__
