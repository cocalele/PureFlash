#ifndef s5_md5_h__
#define s5_md5_h__
#include <stdlib.h>
#include "s5_utils.h"

class MD5_CTX;
typedef int dev_handle_t;

class MD5Stream
{
	dev_handle_t dev_fd;
	off_t base_offset;
	char* buffer;
public:
	MD5Stream(dev_handle_t dev_fd);
	~MD5Stream();
	int init();
	void destroy();
	void reset(off_t offset);
	ssize_t read(void *buf, size_t count, off_t offset);
	ssize_t write(void *buf, size_t count, off_t offset);

	//finalize the md5 calculation with a 0 block, then write the md5 checksum to disk at position: offset.
	int finalize(off_t offset, int is_read);
};
#endif // s5_md5_h__
