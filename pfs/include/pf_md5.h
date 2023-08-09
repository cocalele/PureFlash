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

class MD5_CTX;
typedef int dev_handle_t;

class MD5Stream
{
	union {
		int fd;
		struct {
			PfIoEngine *ioengine;
		} nvme;
	};
	off_t base_offset;
	char* buffer;
	bool spdk_engine;
public:
	MD5Stream(int fd);
	~MD5Stream();
	void spdk_eng_init(PfIoEngine *eng);
	int init();
	void destroy();
	void reset(off_t offset);
	int read(void *buf, size_t count, off_t offset);
	int write(void *buf, size_t count, off_t offset);

	//finalize the md5 calculation with a 0 block, then write the md5 checksum to disk at position: offset.
	int finalize(off_t offset, int is_read);
};
#endif // pf_md5_h__
