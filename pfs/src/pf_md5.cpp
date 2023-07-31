/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include "pf_md5.h"
#include "basetype.h"
#include "pf_main.h"
#include "pf_spdk.h"

MD5Stream::MD5Stream(int fd)
{
	this->fd=fd;
	buffer = NULL;
	spdk_engine = false;
	reset(0);
}

MD5Stream::~MD5Stream()
{
	if (spdk_engine)
		spdk_dma_free(buffer);
	else
		free(buffer);
}

void MD5Stream::spdk_eng_init(PfIoEngine *eng)
{
	this->nvme.ioengine = eng;
	spdk_engine = true;
}

int MD5Stream::init()
{
	int rc =0;
	if (spdk_engine)
		buffer = (char *)spdk_dma_zmalloc(LBA_LENGTH, LBA_LENGTH, NULL);
	else 
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
	uint64_t rc;

	if (app_context.engine == SPDK) {
		if ((rc = nvme.ioengine->sync_read(buf, count, offset)) != count)
			return -1;
		return 0;
	}
	if (-1 == pread(fd, buf, count, base_offset + offset))
		return -errno;
	return 0;
}

int MD5Stream::write(void *buf, size_t count, off_t offset)
{
	uint64_t rc;

	if (spdk_engine) {
		if ((rc = nvme.ioengine->sync_write(buf, count, offset)) != count)
			return -1;
		return 0;
	}
	if (-1 == pwrite(fd, buf, count, base_offset + offset))
		return -errno;
	return 0;
}

int MD5Stream::finalize(off_t offset, int in_read)
{

	return  0;

}
