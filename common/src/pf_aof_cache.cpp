#include <mutex>
#include <string.h>
#include <future>

#include "pf_aof_cache.h"
#include "pf_aof.h"
#include "pf_utils.h"
#include "pf_log.h"
#define OFF2SLOT(x) (((x)/SLOT_SIZE)%SLOT_CNT)
#define SLOT_SIZE_MASK (SLOT_SIZE - 1)
#define ALIGN_ON_SLOT(offset) ((offset)& (~SLOT_SIZE_MASK))

int AofWindowCache::init(PfAof* aof, bool _prefetch)
{
	int rc = 0;
	for(int i=0;i<SLOT_CNT;i++){
		rc = slots[i].init(aof);
		if(rc){
			S5LOG_FATAL("Failed to init %d slot, rc:%d", rc);
		}

	}
	this->prefetch = _prefetch;
	this->aof=aof;
	return rc;
}

size_t AofWindowCache::pread(void* buf, size_t len, off_t offset)
{
	if (offset >= aof->file_length()) {
		S5LOG_WARN("Read offset:%ld exceed file len:%ld", offset, aof->file_length());
		return 0;
	}
	if (offset + len > aof->file_length()) {
		S5LOG_WARN("Read exceed file end, set len from %ld to %ld", len, aof->file_length() - offset);
		len = aof->file_length() - offset;
	}

	size_t remain = len;
	size_t readed = 0;
	while(remain >0) {
		
		size_t sz = slots[OFF2SLOT(offset)].pread((char*)buf + readed, remain, offset);
		offset += sz;
		readed += sz;
		remain -= sz;
	}
	if(prefetch && (offset + (SLOT_SIZE/2) >= ALIGN_ON_SLOT(offset + SLOT_SIZE)) ){ //reading near the end
		slots[OFF2SLOT(offset+SLOT_SIZE)].bg_fetch_data(ALIGN_ON_SLOT(offset + SLOT_SIZE));
	}
	return len;
}

int CacheLine::init(PfAof* aof)
{
	off_in_file = -1;
	buf = aligned_alloc(4096, SLOT_SIZE);
	if(buf == NULL)
		return -ENOMEM;
	this->aof=aof;
	hit_cnt = 0;
	return 0;
}
static int hit_stat[8];
static int read_cnt;

size_t CacheLine::pread(void* buf, size_t len, off_t offset/*, int* disk_accessed*/)
{
retry_read:
	lock.lock_shared();
	if(ALIGN_ON_SLOT(offset) != off_in_file){
		lock.unlock_shared();
		if(hit_cnt >= 5) hit_stat[5]++;
		else hit_stat[hit_cnt]++;
		if(++read_cnt % 16384 == 0){
			S5LOG_DEBUG("cache line hit statistics: 0:%d 1:%d 2:%d 3:%d 4:%d >=5:%d", 
				hit_stat[0], hit_stat[1], hit_stat[2], hit_stat[3], hit_stat[4], hit_stat[5]);
		}
		int rc = fetch_data(ALIGN_ON_SLOT(offset));
		if(rc != 0){
			S5LOG_ERROR("Failed to fetch data, rc:%d", rc);
			return rc;
		}
		//*disk_accessed = 1;
		goto retry_read;
	}
	hit_cnt ++;
	//else
	//	*disk_accessed = 0;
	size_t l = std::min((int64_t)len, (int64_t)(SLOT_SIZE - (offset - off_in_file)));
	memcpy(buf, (char*)this->buf + offset - off_in_file, l);
	lock.unlock_shared();
	return l;

}

int CacheLine::fetch_data(off_t offset)
{
	lock.lock();
	DeferCall _r([this](){lock.unlock();});
	if(this->off_in_file == offset)
		return 0;
	int rc = (int)aof->read(buf, SLOT_SIZE, offset);
	if(rc >= 0){
		this->off_in_file = offset;
		hit_cnt = 0;

	} else {
		this->off_in_file = -1;
		S5LOG_ERROR("Failed fetch aof data, rc:%d", rc);
		return rc;
	}
	return 0;
}

int CacheLine::bg_fetch_data(off_t offset)
{
	lock.lock_shared();
	if ((offset & (~SLOT_SIZE_MASK)) != off_in_file) {
		std::ignore = std::async(std::launch::async, [this, offset](){
			int rc = fetch_data(offset & (~SLOT_SIZE_MASK));
			if (rc != 0) {
				S5LOG_ERROR("Failed to fetch data, rc:%d", rc);
			}
		});
	}
	lock.unlock_shared();
	return 0;


}