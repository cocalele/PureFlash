#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <istream>
#include <vector>
#include <string>
#include <iostream>
#include <climits>

#include "pf_utils.h"
#include "pf_client_api.h"
#include "pf_aof.h"

using namespace std;
static PfAof* aof = NULL;

struct cmd{
	char op;
	uint64_t length;
	string src_file;
	uint64_t src_offset;
	uint64_t vol_offset;
};


static int do_append(cmd& c)
{
	printf("append at:%ld from file:%s:%ld\n", aof->file_length(), c.src_file.c_str(), c.src_offset);
	int f = open(c.src_file.c_str(), O_RDONLY);
	if(f<0){
		fprintf(stderr, "Failed to open file:%s, (%d):%s", c.src_file.c_str(), errno, strerror(errno));
		return -errno;
	}
	DeferCall _c([f]() { close(f); });

	void* buf = malloc(c.length);
	if(buf == NULL){
		fprintf(stderr, "Failed alloc memory:%ld", c.length);
		return -ENOMEM;
	}
	DeferCall _c2([buf]() { free(buf); });
	ssize_t r = pread(f, buf, c.length, c.src_offset);
	if(r != c.length){
		fprintf(stderr, "Failed to read file:%s, %ld  rc:%d", c.src_file.c_str(), r, -errno);
		return -errno;
	}
	r  = aof->append(buf, c.length);
	if (r != c.length) {
		fprintf(stderr, "Failed to append aof, %ld  rc:%d",  r, -errno);
		return -errno;
	}
	return 0;
}
int do_sync()
{
	aof->sync();
	return 0;
}
int do_read(cmd& c)
{
	printf("write at:%ld to file:%s:%ld\n", c.src_offset,  c.src_file.c_str(), c.length);
	int f = open(c.src_file.c_str(), O_RDWR|O_CREAT, 0666);
	if (f < 0) {
		fprintf(stderr, "Failed to open file:%s, (%d):%s", c.src_file.c_str(), errno, strerror(errno));
		return -errno;
	}
	DeferCall _c([f]() { close(f); });

	void* buf = malloc(c.length);
	if (buf == NULL) {
		fprintf(stderr, "Failed alloc memory:%ld", c.length);
		return -ENOMEM;
	}
	DeferCall _c2([buf]() { free(buf); });
	ssize_t r = aof->read(buf, c.length, c.vol_offset);
	if (r < 0) {
		fprintf(stderr, "Failed to append aof, %ld  rc:%d", r, -errno);
		return -errno;
	}
	//S5LOG_INFO("Read buf:%p first QWORD:0x%lx", buf, *(long*)buf);
	ssize_t r2 = pwrite(f, buf, r, c.src_offset);
	if (r2 != r) {
		fprintf(stderr, "Failed to read file:%s, %ld  rc:%d", c.src_file.c_str(), r, -errno);
		return -errno;
	}
	fsync(f);
	return 0;
}
int main(int argc, char** argv)
{
	char buf[1024];
	int rc = 0;
	if(argc != 3) {
		fprintf(stderr, "Usage:%s <aof_name> <conf_file>\n", argv[0]);
		return EINVAL;
	}
	aof = pf_open_aof(argv[1], NULL, O_CREAT, argv[2], S5_LIB_VER);
	if(aof == NULL) {
		fprintf(stderr, "Failed to open aof:%s\n", argv[1]);
		return EINVAL;
	}
	while (rc == 0) {
		cin.getline(buf, sizeof(buf));
		if((cin.rdstate() & std::ios_base::eofbit) != 0) {
			fprintf(stderr, "stdin EOF \n");
			return 0;
		}
		fprintf(stderr, "get line:%s\n", buf);
		vector<string> args = split_string(buf, ' ');
		if (args.size() < 1)
			continue;
		if (args[0].size() > 1) {
			fprintf(stderr, "Invalid cmd line:%s", buf);
			return EINVAL;
		}
		cmd c;
		c.op = args[0][0];
		switch (c.op) {
		case 'a':
			assert(args.size() == 4);
			c.length = strtol(args[1].c_str(), NULL, 10);
			assert(errno == 0);
			c.src_file = args[2];
			c.src_offset = strtol(args[3].c_str(), NULL, 10);
			assert(errno == 0);
			rc = do_append(c);
			break;
		case 's':
			assert(args.size() == 1);
			rc = do_sync();
			break;
		case 'r':
			assert(args.size() == 5);
			c.length = strtol(args[1].c_str(), NULL, 10);
			assert(c.length > 0 && c.length != LONG_MAX);
			c.vol_offset = strtol(args[2].c_str(), NULL, 10);
			assert(c.vol_offset >= 0 && c.vol_offset != LONG_MAX);
			c.src_file = args[3];
			c.src_offset = strtol(args[4].c_str(), NULL, 10);
			assert(c.src_offset >= 0 && c.src_offset != LONG_MAX);
			rc = do_read(c);
			break;
		case 'q':
			exit(0);
		default:
			fprintf(stderr, "Invalid cmd line:%s, len:%ld\n", buf, strlen(buf));
			exit(1);
		}
	}
	return 0;
}
