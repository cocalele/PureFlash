#include <mutex>
#include <string.h>

#include "pf_aof_cache.h"
#include "pf_aof.h"
#include "pf_utils.h"
#include "pf_log.h"
#define OFF2SLOT(x) ((x/SLOT_SIZE)%SLOT_CNT)
#define SLOT_SIZE_MASK (SLOT_SIZE - 1)

int AofWindowCache::init(PfAof* aof)
{
	int rc = 0;
	for(int i=0;i<SLOT_CNT;i++){
		rc = slots[i].init(aof);
		if(rc){
			S5LOG_FATAL("Failed to init %d slot, rc:%d", rc);
		}

	}
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
		
		size_t sz = slots[OFF2SLOT(offset)].pread((char*)buf + readed, remain, offset + readed);
		readed += sz;
		remain -= sz;
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
	return 0;
}

size_t CacheLine::pread(void* buf, size_t len, off_t offset)
{
retry_read:
	lock.lock_shared();
	if((offset & (~SLOT_SIZE_MASK)) != off_in_file){
		lock.unlock_shared();
		int rc = fetch_data(offset & (~SLOT_SIZE_MASK));
		if(rc != 0){
			S5LOG_ERROR("Failed to fetch data, rc:%d", rc);
			return rc;
		}
		goto retry_read;
	}
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
	} else {
		S5LOG_ERROR("Failed fetch aof data, rc:%d", rc);
		return rc;
	}
	return 0;
}
