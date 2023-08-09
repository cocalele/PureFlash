#ifndef _S5_BUFFER_H_
#define _S5_BUFFER_H_
/**
 * Copyright (C), 2019.
 * @endcode GBK
 * @file
 * һ��buffer����һ���������ڴ档һ�������һ��ָ�����һ�����ȾͿ�������һ���ڴ档��ʹ��RDMA����ʱ
 * ����ڴ���Ҫ�������Ϣ����local key, remote key, offset���ҶԲ�ͬ��RDMA�豸��ע�����и��Զ�Ӧ
 * ��local key, remote key������������ͬһ��buffer������ͬ���豸ʹ�þ�Ҫ�ṩ��ͬ�ķ���Ҫ�ء������
 * buffer_descriptor ���ڵ����塣buffer_descriptor�����¼�����buffer���ֳ����µķ�����Ϣ��
 *
 * buffer �ᰴ����󳤶ȷ��䣬����������������IO��64K byte�� ��ôbufer�������64K byte��Ҳ����buf_size
 * ��65536��Ȼ��������һ��4K byte IO����ʱ����Ч���ݵĳ���,��data_len��4096
 */

#include "pf_fixed_size_queue.h"

class BufferPool;
class PfConnection;

//Work complete status
enum WcStatus {
	TCP_WC_SUCCESS = 0,
	TCP_WC_FLUSH_ERR = 5,
	RDMA_WC_SUCCESS = 10
};
const char* WcStatusToStr(WcStatus s);

//Work request op code
enum WrOpcode {
	TCP_WR_SEND = 0,
	TCP_WR_RECV = 128,
	RDMA_WR_SEND = 129,
	RDMA_WR_RECV = 130,
	RDMA_WR_WRITE = 131,
	RDMA_WR_READ = 132
};
const char* OpCodeToStr(WrOpcode op) ;

typedef void(*completion_handler)(int status, int opcode, void* data);

struct BufferDescriptor
{
	WrOpcode wr_op;// work request op code
	union {
		void* buf;
		struct PfMessageHead* cmd_bd; //valid if thie BD used for command
		struct PfMessageReply* reply_bd; //valid if this BD used for message reply
	};
	int data_len; /// this is the validate data len in the buffer.
	union {
		struct PfClientIocb *client_iocb;
		struct PfServerIocb *server_iocb;
	};
	//int(*on_work_complete)(BufferDescriptor* bd, WcStatus complete_status, PfConnection* conn, void* cbk_data);
	void* cbk_data;
	int buf_capacity; /// this is the size, i.e. max size of buf
#ifdef WITH_RDMA
	struct ibv_mr* mrs[4];
#endif
	BufferPool* owner_pool;
	PfConnection* conn;
};

class BufferPool
{
public:
	BufferPool(){dma_buffer_used = 0;}
	size_t buf_size;
	int buf_count;
	int dma_buffer_used;
	struct ibv_mr* mrs[4];
	int init(size_t buffer_size, int count);
	inline BufferDescriptor* alloc() { return free_bds.dequeue(); }
	inline int free(BufferDescriptor* bd){ bd->client_iocb = NULL; return free_bds.enqueue(bd); }
	void destroy();
	void* data_buf;
	BufferDescriptor* data_bds;
private:
	PfFixedSizeQueue<BufferDescriptor*> free_bds;
};

struct disp_mem_pool
{
	BufferPool cmd_pool;
	BufferPool data_pool;
	BufferPool reply_pool;
};

struct replicator_mem_pool
{
	BufferPool cmd_pool;
	BufferPool reply_pool;
};
#endif //_S5_BUFFER_H_

