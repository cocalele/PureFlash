#ifndef pf_aof_h__
#define pf_aof_h__

#include "pf_volume.h"
class  PfAof : public PfClientVolumeInfo
{
private:
	void* append_buf;
	off_t append_tail;//append tail in buffer
	ssize_t file_len;
	union{
	void* head_buf;
	struct PfAofHead* head;
	};
	ssize_t append_buf_size;
public:
	PfAof(ssize_t append_buf_size = 2 << 20);
	~PfAof();
	ssize_t append(const void* buf, ssize_t len);
	void sync();
private:
	int open();
	//ssize_t sync_write(const void* buf, size_t count, off_t offset);
	//ssize_t sync_read(const void* buf, size_t count, off_t offset);
	friend PfAof* pf_open_aof((const char* volume_name, const char* cfg_filename, int lib_ver);
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
PfAof* pf_open_aof(const char* volume_name, const char* cfg_filename, int lib_ver);

71 | PfAof * pf_o
#endif // pf_aof_h__
