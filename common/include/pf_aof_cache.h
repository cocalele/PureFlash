#ifndef pf_aof_cache_h__
#define pf_aof_cache_h__
#include <shared_mutex>
#include <stdint.h>
#include <unistd.h> //for off_t type

class PfAof;

#define SLOT_CNT 4
#define SLOT_SIZE (PF_MAX_IO_SIZE * 4) //i.e. 128K x 4 = 512K


class CacheLine
{
public:
	//size_t buf_size; //always SLOT_SIZE
	off_t off_in_file;
	void* buf;
	PfAof* aof;
	std::shared_mutex lock;

	int init(PfAof* aof);
	size_t pread(void* buf, size_t len, off_t offset);
	int fetch_data(off_t offset);
};

class AofWindowCache
{
public:

	CacheLine slots[SLOT_CNT]; //a 2M buffer
	PfAof* aof;

	int init(PfAof* aof);
	size_t pread(void* buf, size_t len, off_t offset);
private:
	int prefetch_one_slot();

};
#endif // pf_aof_cache_h__