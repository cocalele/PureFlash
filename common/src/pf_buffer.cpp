#include <exception>
#include "pf_buffer.h"

using namespace std;

int BufferPool::init(size_t buffer_size, int count)
{
	int rc = 0;
	Cleaner clean;
	rc = free_bds.init(count);
	if(rc != 0)
		throw std::runtime_error(format_string("init memory pool failed, rc:%d", rc));
	clean.push_back([this](){free_bds.destroy(); });
	data_buf = malloc(buffer_size*count);
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
		data_bds[i].buf_size = (int)buffer_size;
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