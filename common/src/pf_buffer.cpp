#include <exception>
#include <malloc.h>
#include <string.h>
#include "pf_buffer.h"

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
	data_buf = memalign(4096, buffer_size*count);
	if(data_buf == NULL)
		throw std::runtime_error(format_string("Failed to alloc memory of:%d bytes", buffer_size*count));
	clean.push_back([this](){ ::free(data_buf); });
	data_bds = (BufferDescriptor*)calloc(count, sizeof(BufferDescriptor));
	if(data_bds == NULL)
		throw std::runtime_error(format_string("Failed to alloc memory of:%d bytes", count * sizeof(BufferDescriptor)));
	clean.push_back([this](){ ::free(data_bds); });
	for(int i=0;i<count;i++)
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
	::free(data_buf);
	free_bds.destroy();
}

const char* WcStatusToStr(WcStatus s) {
	switch(s){
		case TCP_WC_SUCCESS:
			return "TCP_WC_SUCCESS";
		case TCP_WC_FLUSH_ERR:
			return "TCP_WC_FLUSH_ERR";
        case RDMA_WC_SUCCESS:
            return "RDMA_WC_SUCCESS";
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
	}
	S5LOG_ERROR("Unknown op code:%d", op);
	return "Unknown";
}
