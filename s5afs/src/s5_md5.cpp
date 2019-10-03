#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include "s5_md5.h"

void MD5Stream::MD5Stream(dev_handle_t dev_fd)
{
	this->dev_fd = dev_fd;
	buffer = NULL;
	reset(0);
}
void MD5Stream::~MD5Stream()
{
	free(buffer);
}

int MD5Stream::init()
{
	int rc =0;
	buffer  = (char*)aligned_alloc(PAGE_SIZE, PAGE_SIZE);
	if (buffer == NULL)
	{
		S5LOG_ERROR("Failed to allocate memory for MD5Stream");
		return -ENOMEM;
	}
	return rc;
}

void MD5Stream::reset(off_t offset)
{
	base_offset=offset;
}


ssize_t MD5Stream::read(void *buf, size_t count, off_t offset)
{
	if (-1 == pread(dev_fd, buf, count, base_offset + offset))
		return -errno;
	return 0;
}

ssize_t MD5Stream::write(void *buf, size_t count, off_t offset)
{
	if (-1 == pwrite(dev_fd, buf, count, base_offset + offset))
		return -errno;
	return 0;
}

int MD5Stream::finalize(off_t offset, int in_read)
{

	return  0;

}
