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

int MD5Stream::alloc_buffer()
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
	data_len = 0;
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


int MD5Stream_ISA_L::init()
{
	int rc =0;

	rc = alloc_buffer();
	if (rc)
	{
		S5LOG_ERROR("Failed to allocate buffer");
		return rc;
	}
	mgr = (MD5_HASH_CTX_MGR *)aligned_alloc(16, sizeof(MD5_HASH_CTX_MGR));
	if (!mgr)
	{
		S5LOG_ERROR("Failed to allocate memory for mgr");
		return -ENOMEM;
	}
	md5_ctx_mgr_init(mgr);
	hash_ctx_init(&ctxpool);

	return rc;
}


int MD5Stream_ISA_L::write_calc(void *buf, size_t count, off_t offset)
{
	int rc;

	rc = write(buf, count, offset);
	if (rc) {
		S5LOG_ERROR("Failed to write");
		return rc;
	}

	md5_ctx_mgr_submit(mgr, &ctxpool, buf, (uint32_t)count, data_len == 0 ? HASH_FIRST : HASH_UPDATE);
	while (md5_ctx_mgr_flush(mgr));

	data_len += count;

	return 0;
}

int MD5Stream_ISA_L::read_calc(void *buf, size_t count, off_t offset)
{
	int rc;

	rc = read(buf, count, offset);
	if (rc) {
		S5LOG_ERROR("Failed to read from offset:%lu, rc:%d", offset, rc);
		return rc;
	}

	md5_ctx_mgr_submit(mgr, &ctxpool, buf, (uint32_t)count, data_len == 0 ? HASH_FIRST : HASH_UPDATE);
	while (md5_ctx_mgr_flush(mgr));

	data_len += count;

	return 0;
}

int MD5Stream_ISA_L::finalize(char *result, int in_read)
{
	int i;
	char result_buf[MD5_RESULT_LEN] = {0};
	int rc = 0;

	if (MD5_RESULT_LEN != sizeof(ctxpool.job.result_digest)) {
		S5LOG_FATAL("md5 buffer not enough, expect:%d", sizeof(ctxpool.job.result_digest));
	}

	md5_ctx_mgr_submit(mgr, &ctxpool, const_zero_page, 4096, HASH_LAST);
	while (md5_ctx_mgr_flush(mgr));

	for (i = 0; i < MD5_DIGEST_NWORDS; i++) {
		((uint32_t *)result_buf)[i] = ctxpool.job.result_digest[i];
	}

	if (in_read) {
		rc = memcmp(result, result_buf, MD5_RESULT_LEN);
		if (rc) {
			S5LOG_ERROR("md5 mismath");
		}
	}else{
		memcpy(result, result_buf, MD5_RESULT_LEN);
	}

	return rc;
}
