#ifndef pf_aof_h__
#define pf_aof_h__

#include <semaphore.h>
#include <vector>
#include "pf_client_api.h"
class PfAof;

PfAof* pf_open_aof(const char* volume_name, const char* snap_name, int flags, const char* cfg_filename, int lib_ver);
int pf_aof_access(const char* volume_name, const char* cfg_filename);
int pf_ls_aof_children(const char* tenant_name, const char* cfg_filename, std::vector<std::string>* result);
int pf_rename_aof(const char* volume_name, const char* new_name, const char* pf_cfg_file);
int pf_delete_aof(const char* volume_name, const char* pf_cfg_file);

//#define _DATA_DBG
class SimpleCache;

class  PfAof
{
public:
	PfClientVolume* volume;
private:
	void* append_buf;
	off_t append_tail;//append tail in buffer
	ssize_t file_len;
//	mutable void* read_buf;//a small buffer to read unaligned part
	union{
	void* head_buf;
	struct PfAofHead* head;
	};
	ssize_t append_buf_size;
public:
	PfAof(ssize_t append_buf_size = 2 << 20);
	ssize_t append(const void* buf, ssize_t len);
	ssize_t read(void* buf, ssize_t len, off_t offset) const;
	void sync();
	inline ssize_t file_length() { return file_len; }
	const char* path(); //return file full path include name
	int reader_cnt = 0;
	int writer_cnt = 0;
	int ref_count = 1;
	inline void add_ref() {
		__sync_fetch_and_add(&ref_count, 1);
	}
	inline void dec_ref() {
		if (__sync_sub_and_fetch(&ref_count, 1) == 0) {
			delete this;
		}
	}

private:
	~PfAof();
	int open();
	friend PfAof* pf_open_aof(const char* volume_name, const char* snap_name, int flags, const char* cfg_filename, int lib_ver);
#ifdef _DATA_DBG
	int localfd;
#else
	int _holder;
#endif
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
