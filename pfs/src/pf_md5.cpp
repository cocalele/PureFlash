#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include "pf_md5.h"
#include "basetype.h"

MD5Stream::MD5Stream(int fd)
{
	this->fd=fd;
	buffer = NULL;
	reset(0);
}
MD5Stream::~MD5Stream()
{
	free(buffer);
}

int MD5Stream::init()
{
	int rc =0;
	buffer  = (char*)aligned_alloc(LBA_LENGTH, LBA_LENGTH);
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


int MD5Stream::read(void *buf, size_t count, off_t offset)
{
	if (-1 == pread(fd, buf, count, base_offset + offset))
		return -errno;
	return 0;
}

int MD5Stream::write(void *buf, size_t count, off_t offset)
{
	if (-1 == pwrite(fd, buf, count, base_offset + offset))
		return -errno;
	return 0;
}

int MD5Stream::finalize(off_t offset, int in_read)
{

	return  0;

}
