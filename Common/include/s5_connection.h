#ifndef s5_connection_h__
#define s5_connection_h__
class S5Connection
{
public:
	int ref_count;
	virtual int post_recv(BufferDescriptor* buf)=0;
	virtual int post_send(BufferDescriptor* buf)=0;
	virtual int post_read(BufferDescriptor* buf)=0;
	virtual int post_write(BufferDescriptor* buf)=0;
	virtual int on_destroy() = 0;
	virtual int on_close() = 0;
	virtual int destroy();
	virtual int close();

	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
};

#endif // s5_connection_h__
