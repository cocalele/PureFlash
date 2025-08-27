#include <exception>
#include <malloc.h>
#include <string.h>
#include "pf_buffer.h"

#include "spdk/env.h"

using namespace std;

int BufferPool::init(size_t buffer_size, int count)
{
	int rc = 0;
	Cleaner clean;
	this->buf_size = buffer_size;
	this->buf_count = count;
	memset(mrs, 0, 4*sizeof(struct ibv_mr*));
	rc = free_bds.init(count);
	if(rc != 0)
		throw std::runtime_error(format_string("init memory pool failed, rc:%d", rc));
	clean.push_back([this](){free_bds.destroy(); });
	if (dma_buffer_used) {
		data_buf = spdk_dma_zmalloc(buffer_size*count, 4096, NULL);
		if(data_buf == NULL)
			throw std::runtime_error(format_string("Failed to alloc memory of:%d bytes", buffer_size*count));
		clean.push_back([this](){ ::spdk_dma_free(data_buf); });		
	} else {
		data_buf = memalign(4096, buffer_size*count);
		if(data_buf == NULL)
			throw std::runtime_error(format_string("Failed to alloc memory of:%d bytes", buffer_size*count));
		clean.push_back([this](){ ::free(data_buf); });
	}

	data_bds = (BufferDescriptor*)calloc(count, sizeof(BufferDescriptor));
	if(data_bds == NULL)
		throw std::runtime_error(format_string("Failed to alloc memory of:%d bytes", count * sizeof(BufferDescriptor)));
	clean.push_back([this](){ ::free(data_bds); });
	for(int i = 0; i < count; i++)
	{
		data_bds[i].buf = (char*)data_buf + buffer_size * i;
		data_bds[i].buf_capacity = (int)buffer_size;
		data_bds[i].owner_pool = this;
		free_bds.enqueue(&data_bds[i]);
	}
	clean.cancel_all();
	return 0;
}


void BufferPool::destroy()
{
	::free(data_bds);
	if (dma_buffer_used) {
		spdk_dma_free(data_buf);
	} else {
		::free(data_buf);
	}

	free_bds.destroy();
}

int BufferPool::rmda_register_mr(struct ibv_pd* pd, int idx, int access_mode)
{
	if (mrs[idx] != NULL)
	{
		S5LOG_ERROR("pool->mrs[%d] is not NULL", idx);
		return -EEXIST;
	}
	struct ibv_mr *mr = mrs[idx] = ibv_reg_mr(pd, data_buf, buf_size * buf_count, access_mode);
	if (!mr) {
		S5LOG_ERROR("ibv_reg_mr failed, idx;%d, errno:%d", idx, errno);
        return -errno;
	}
	for (int i = 0 ;i < buf_count; i++) {
		data_bds[i].mrs[idx] = mr;
	}

	return 0;
}

void BufferPool::rmda_unregister_mr()
{
	for (int i = 0 ; i < 4; i++) {
		if (mrs[i] != NULL)
			ibv_dereg_mr(mrs[i]);
	}

	return ;
}

const char* WcStatusToStr(WcStatus s) {
	switch(s){
		case WC_SUCCESS:
			return "WC_SUCCESS";
		case WC_FLUSH_ERR:
			return "WC_FLUSH_ERR";
	}
	S5LOG_ERROR("Unknown WcStatus:%d", s);
	return "Unknown";
}
const char* OpCodeToStr(WrOpcode op) {
	switch(op) {
		case TCP_WR_SEND:
			return "TCP_WR_SEND";
		case TCP_WR_RECV:
			return "TCP_WR_RECV";
		case RDMA_WR_SEND:
			return "RDMA_WR_SEND";
		case RDMA_WR_RECV:
			return "RDMA_WR_RECV";
		case RDMA_WR_WRITE:
			return "RDMA_WR_WRITE";
		case RDMA_WR_READ:
			return "RDMA_WR_READ";
	}
	S5LOG_ERROR("Unknown op code:%d", op);
	return "Unknown";
}
