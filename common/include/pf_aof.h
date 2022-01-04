#ifndef pf_aof_h__
#define pf_aof_h__

#include "pf_client_api.h"
class PfAof;

PfAof* pf_open_aof(const char* volume_name, const char* snap_name, int flags, const char* cfg_filename, int lib_ver);


class  PfAof
{
private:
	PfClientVolume* volume;
	void* append_buf;
	off_t append_tail;//append tail in buffer
	ssize_t file_len;
	void* read_buf;//a small buffer to read unaligned part
	union{
	void* head_buf;
	struct PfAofHead* head;
	};
	ssize_t append_buf_size;
public:
	PfAof(ssize_t append_buf_size = 2 << 20);
	~PfAof();
	ssize_t append(const void* buf, ssize_t len);
	ssize_t read(void* buf, ssize_t len, off_t offset);
	void sync();
	inline ssize_t file_length() { return file_len; }
private:
	int open();
	//ssize_t sync_write(const void* buf, size_t count, off_t offset);
	//ssize_t sync_read(const void* buf, size_t count, off_t offset);
	friend PfAof* pf_open_aof(const char* volume_name, const char* snap_name, int flags, const char* cfg_filename, int lib_ver);
};

struct PfAofHead
{
	uint32_t magic;
	uint32_t version;
	uint64_t length;
	uint64_t modify_time;
	uint64_t access_time;
	uint64_t create_time;
};

#endif // pf_aof_h__
