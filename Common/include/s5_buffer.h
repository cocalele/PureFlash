#ifndef _S5_BUFFER_H_
#define _S5_BUFFER_H_
/**
 * Copyright (C), 2019.
 * @endcode GBK
 * @file
 * 一个buffer就是一块连续的内存。一般情况下一个指针加上一个长度就可以描述一块内存。在使用RDMA访问时
 * 这个内存需要额外的信息，即local key, remote key, offset。且对不同的RDMA设备，注册后会有各自对应
 * 的local key, remote key。这样，对于同一个buffer，给不同的设备使用就要提供不同的访问要素。这就是
 * buffer_descriptor 存在的意义。buffer_descriptor里面记录了这个buffer各种场景下的访问信息。
 *
 * buffer 会按照最大长度分配，比如我们允许的最大IO是64K byte， 那么bufer将会分配64K byte，也就是buf_size
 * 是65536。然而当处理一个4K byte IO请求时，有效数据的长度,即data_len是4096
 */

#include "s5_fixed_size_queue.h"

class BufferPool;
class S5Connection;

//Work complete status
enum WcStatus {
	TCP_WC_SUCCESS = 0,
	TCP_WC_FLUSH_ERR = 5,
};

//Work request op code
enum WrOpcode {
	TCP_WR_SEND = 0,
	TCP_WR_RECV = 128,
};

struct BufferDescriptor
{
	WrOpcode wr_op;// work request op code
	void* buf;
	int data_len; /// this is the validate data len in the buffer.
	int(*on_work_complete)(BufferDescriptor* bd, WcStatus complete_status, S5Connection* conn, void* cbk_data);
	void* cbk_data;
	int buf_size; /// this is the size, i.e. max size of buf
	BufferPool* owner_pool;

};

class BufferPool
{
public:
	int init(size_t buffer_size, size_t count);
	BufferDescriptor* alloc();
	int free(BufferDescriptor* bd);
private:
	S5FixedSizeQueue<BufferDescriptor*> free_bds;
	void* data_buf;
};
#endif //_S5_BUFFER_H_

