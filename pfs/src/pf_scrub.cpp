//
// Created by liu_l on 12/26/2020.
//
#include <stdint.h>
#include <stdlib.h>
#include <libaio.h>
#include <unistd.h>

#include "pf_scrub.h"
#include "pf_flash_store.h"

#define PARALLEL_NUM 8
#define MD5_BUF_LEN (128<<10)
Scrub::Scrub() noexcept
{
	ctxpool = (HASH_CTX *) calloc(PARALLEL_NUM, sizeof(HASH_CTX));
	for (uint64_t i = 0; i < PARALLEL_NUM; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}
	posix_memalign((void **)&mgr, 16, sizeof(HASH_CTX_MGR));
	md5_ctx_mgr_init(mgr);
}

int Scrub::feed_data(void* buf, size_t len, size_t off)
{
	assert(len == MD5_BUF_LEN);
	HASH_CTX_FLAG flag=HASH_UPDATE;
	int64_t ctx_idx = (off/MD5_BUF_LEN)%PARALLEL_NUM;

	if(off/(MD5_BUF_LEN*PARALLEL_NUM) == 0)
		flag = HASH_UPDATE;
	size_t seg_len = len / PARALLEL_NUM;
	CTX_MGR_SUBMIT(mgr, &ctxpool[ctx_idx], buf, len, flag);

}

std::string Scrub::cal_replica(PfFlashStore *s, replica_id_t rep_id) {
	int iodepth = PARALLEL_NUM;
	size_t read_size = MD5_BUF_LEN*PARALLEL_NUM;
	void* read_buf = memalign(LBA_LENGTH, read_size);
	if(read_buf == NULL) {
		S5LOG_ERROR("Failed to alloc scrub buf, size:%lld", read_size);
		return "";
	}
	DeferCall _c([read_buf](){free(read_buf);});

	uint64_t base_off = rep_id.shard_index()*SHARD_SIZE;

	for(int64_t obj_idx = 0; obj_idx < SHARD_SIZE/s->head.objsize; obj_idx ++){
		lmt_key key = {.vol_id=rep_id.to_volume_id().vol_id, .slba=(obj_idx*s->head.objsize+base_off)/LBA_LENGTH};
//		sem_t recov_sem;
//		sem_init(&recov_sem, 0, iodepth);

		off_t obj_offset;
		EntryStatus head_status;
		int rc = s->sync_invoke([s, &key, &obj_offset, &head_status]()->int{
			auto pos = s->obj_lmt.find(key);
			if(pos == s->obj_lmt.end())
				return -ENOENT;
			lmt_entry *head = pos->second;
			if(head->status != EntryStatus::NORMAL) {
				head_status = head->status;
				return -EINVAL;
			}
			obj_offset = head->offset;
			return 0;
		});
		if(rc == -ENOENT)
			continue;
		else if(rc == -EINVAL){
			S5LOG_ERROR("Object{vol_id:0x%x, slba:%lld} in status %d and not ready for md5 calculate", key.vol_id, key.slba,
			            head_status);
			return std::string();
		}
		HASH_CTX_FLAG hash_falg = HASH_UPDATE;
		if(obj_idx == 0)
			HASH_CTX_FLAG hash_falg = HASH_UPDATE;
		for(int64_t read_idx = 0; read_idx < s->head.objsize/read_size; read_idx ++){
			size_t r = pread(s->fd, read_buf, read_size, obj_offset+read_idx*read_size);
			if(r !=  read_size){
				S5LOG_ERROR("Failed read dev:%s, errno:%d", s->tray_name, errno);
			}
			for(int buf_idx = 0; buf_idx < PARALLEL_NUM; buf_idx++){
				md5_ctx_mgr_submit(mgr, &ctxpool[buf_idx], (char *)read_buf + buf_idx * MD5_BUF_LEN , MD5_BUF_LEN, hash_falg);
			}
		}
	}
	for(int64_t read_idx = 0; read_idx < s->head.objsize/read_size; read_idx ++){
		for(int buf_idx = 0; buf_idx < PARALLEL_NUM; buf_idx++){
			md5_ctx_mgr_submit(mgr, &ctxpool[buf_idx], (char *)read_buf + buf_idx * MD5_BUF_LEN , 0, HASH_LAST);
		}
	}
	while (CTX_MGR_FLUSH(mgr));
	char rst[sizeof(ctxpool[0].job.result_digest) * PARALLEL_NUM * 2+4];//extra 4 bytes, only 1 used for \0
	for (int i = 0; i < PARALLEL_NUM; i++)
	{
		for(int j=0;j<sizeof(ctxpool[i].job.result_digest);j++) {
			sprintf(rst + i*sizeof(ctxpool[i].job.result_digest) + j*2, "%02x", *(((char*)&ctxpool[i].job.result_digest) + j));
		}
	}
	return rst;
}
