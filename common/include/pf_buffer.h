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
};
const char* WcStatusToStr(WcStatus s);

//Work request op code
enum WrOpcode {
	TCP_WR_SEND = 0,
	TCP_WR_RECV = 128,
};
const char* OpCodeToStr(WrOpcode op) ;

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
	BufferPool* owner_pool;
	PfConnection* conn;
};

class BufferPool
{
public:
	int init(size_t buffer_size, int count);
	inline BufferDescriptor* alloc() { return free_bds.dequeue(); }
	inline int free(BufferDescriptor* bd){ return free_bds.enqueue(bd); }
	void destroy();
private:
	PfFixedSizeQueue<BufferDescriptor*> free_bds;
	void* data_buf;
	BufferDescriptor* data_bds;
};
#endif //_S5_BUFFER_H_

