/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef pf_md5_h__
#define pf_md5_h__
#include <stdlib.h>
#include "pf_utils.h"
#include "pf_tray.h"
#include "pf_ioengine.h"
#include "md5_mb.h"

class MD5_CTX;
typedef int dev_handle_t;

class MD5Stream
{
public:
	union {
		int fd;
		struct {
			PfIoEngine *ioengine;
		} nvme;
	};
	off_t base_offset;
	char* buffer;
	bool spdk_engine;
	int data_len;
public:
	MD5Stream(int fd);
	~MD5Stream();
	void spdk_eng_init(PfIoEngine *eng);
	int alloc_buffer();
	void destroy();
	void reset(off_t offset);
	int read(void *buf, size_t count, off_t offset);
	int write(void *buf, size_t count, off_t offset);
	virtual int write_calc(void *buf, size_t count, off_t offset) = 0;
	virtual int read_calc(void *buf, size_t count, off_t offset) = 0;
	virtual int finalize(char *result, int is_read) = 0;
};

class MD5Stream_ISA_L : public MD5Stream
{
public:
	MD5Stream_ISA_L(int fd):MD5Stream(fd){};
	MD5_HASH_CTX ctxpool;
	MD5_HASH_CTX_MGR *mgr;
	int init();
	int write_calc(void *buf, size_t count, off_t offset);
	int read_calc(void *buf, size_t count, off_t offset);
	int finalize(char *result, int is_read);
};
#endif // pf_md5_h__
