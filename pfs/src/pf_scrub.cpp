/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//
// Created by liu_l on 12/26/2020.
//
#include <stdint.h>
#include <stdlib.h>
#include <libaio.h>
#include <unistd.h>

#include "pf_scrub.h"
#include "pf_flash_store.h"

using namespace  std;
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
Scrub::~Scrub()
{
	free(mgr);
	free(ctxpool);
}
int Scrub::feed_data(void* buf, size_t len, size_t off)
{
	assert(len == MD5_BUF_LEN);
	HASH_CTX_FLAG flag=HASH_UPDATE;
	int64_t ctx_idx = (off/MD5_BUF_LEN)%PARALLEL_NUM;

	if(off/(MD5_BUF_LEN*PARALLEL_NUM) == 0)
		flag = HASH_UPDATE;
	//size_t seg_len = len / PARALLEL_NUM;
	CTX_MGR_SUBMIT(mgr, &ctxpool[ctx_idx], buf, (uint32_t)len, flag);
	return 0;
}

std::string Scrub::cal_replica(PfFlashStore *s, replica_id_t rep_id) {
	//int iodepth = PARALLEL_NUM;
	size_t read_size = MD5_BUF_LEN*PARALLEL_NUM;
	void* read_buf = memalign(LBA_LENGTH, read_size);
	if(read_buf == NULL) {
		S5LOG_ERROR("Failed to alloc scrub buf, size:%lld", read_size);
		return "";
	}
	DeferCall _c([read_buf](){free(read_buf);});

	uint64_t base_off = rep_id.shard_index()*SHARD_SIZE;

	for(int64_t obj_idx = 0; obj_idx < SHARD_SIZE/s->head.objsize; obj_idx ++){
		lmt_key key = {.vol_id=rep_id.to_volume_id().vol_id, .slba=(int64_t)(obj_idx*s->head.objsize+base_off)/LBA_LENGTH};
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
			hash_falg = HASH_FIRST;
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
	char rst[sizeof(ctxpool[0].job.result_digest) * PARALLEL_NUM * 2 + 4];//extra 4 bytes, only 1 used for \0
	for (int i = 0; i < PARALLEL_NUM; i++)
	{
		for(int j=0;j<sizeof(ctxpool[i].job.result_digest);j++) {
			sprintf(rst + i*sizeof(ctxpool[i].job.result_digest)*2 + j*2, "%02x", *(((char*)&ctxpool[i].job.result_digest) + j));
		}
	}
	rst[sizeof(ctxpool[0].job.result_digest) * PARALLEL_NUM * 2] = 0;
	S5LOG_INFO("Calculate md5 for replica:0x%llx, %s", rep_id.val(), rst);
	return rst;
}
std::string calc_block_md5(int fd, off_t offset, size_t len);


int Scrub::cal_object(PfFlashStore* s, replica_id_t rep_id, int64_t obj_idx, std::list<SnapshotMd5 >& result)
{
	list<tuple<uint32_t, string> > rsts;
	uint64_t base_off = rep_id.shard_index() * SHARD_SIZE;

	
	lmt_key key = { .vol_id = rep_id.to_volume_id().vol_id, .slba = (int64_t)(obj_idx * s->head.objsize + base_off) / LBA_LENGTH };
	//		sem_t recov_sem;
	//		sem_init(&recov_sem, 0, iodepth);

	off_t obj_offset[256];
	uint32_t snap_seq[256];
	int snap_cnt;
	EntryStatus head_status;
	int rc = s->sync_invoke([s, &key, &obj_offset, &head_status, &snap_seq, &snap_cnt]()->int {
		auto pos = s->obj_lmt.find(key);
		if (pos == s->obj_lmt.end())
			return -ENOENT;
		lmt_entry* head = pos->second;
		if (head->status != EntryStatus::NORMAL) {
			head_status = head->status;
			return -EINVAL;
		}
		for(snap_cnt =0; head!=NULL ; head=head->prev_snap, snap_cnt++){
			if(snap_cnt >= S5ARRAY_SIZE(snap_seq)){
				S5LOG_ERROR("Too many snapshot than expected!");
				return -EINVAL;
			}
			obj_offset[snap_cnt] = head->offset;
			snap_seq[snap_cnt] = head->snap_seq;
		}
			
		return 0;
		});
	if (rc == -ENOENT)
		return rc;
	else if (rc == -EINVAL) {
		S5LOG_ERROR("Object{vol_id:0x%x, slba:%lld} in status %d and not ready for md5 calculate", key.vol_id, key.slba,
			head_status);
		return rc;
	}

	for(int i=0;i<snap_cnt;i++){
		string md5 = calc_block_md5(s->fd, obj_offset[i], s->head.objsize);
		if(md5.length() == 0){
			return -EINVAL;
		}
		S5LOG_DEBUG("md5 for snap:%d is %s", snap_seq[i], md5.c_str());
		result.emplace_back(snap_seq[i], std::move(md5));
	}
	return 0;
}


struct InnerScrub {
public:
	InnerScrub() noexcept;
	~InnerScrub();
	MD5_HASH_CTX_MGR* mgr = NULL;
	MD5_HASH_CTX* ctxpool = NULL;
};

InnerScrub::InnerScrub() noexcept
{
	ctxpool = (MD5_HASH_CTX*)calloc(PARALLEL_NUM, sizeof(MD5_HASH_CTX));
	for (uint64_t i = 0; i < PARALLEL_NUM; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void*)((uint64_t)i);
	}
	mgr = (MD5_HASH_CTX_MGR*)aligned_alloc(16, sizeof(MD5_HASH_CTX_MGR));
	md5_ctx_mgr_init(mgr);
}
InnerScrub::~InnerScrub()
{
	free(mgr);
	free(ctxpool);
}

//std::string calc_block_md5(int fd, off_t offset, size_t len)
//{
//	size_t read_size = MD5_BUF_LEN * PARALLEL_NUM;
//	void* read_buf = memalign(LBA_LENGTH, read_size);
//	if (read_buf == NULL) {
//		S5LOG_ERROR("Failed to alloc scrub buf, size:%lld", read_size);
//		return "";
//	}
//	DeferCall _c([read_buf]() {free(read_buf); });
//	assert(len% read_size==0);
//	HASH_CTX_FLAG hash_flag = HASH_UPDATE;
//
//
//	InnerScrub inner;
//
//	for (int64_t read_idx = 0; read_idx < len / read_size; read_idx++) {
//		if(read_idx == 0)
//			hash_flag = HASH_FIRST;
//		else if(read_idx == len / read_size -1)
//			hash_flag = HASH_LAST;
//		else
//			hash_flag = HASH_UPDATE;
//		size_t r = pread(fd, read_buf, read_size, offset + read_idx * read_size);
//		if (r != read_size) {
//			S5LOG_ERROR("Failed read errno:%d", errno);
//			return "";
//		}
//		for (int buf_idx = 0; buf_idx < PARALLEL_NUM; buf_idx++) {
//			md5_ctx_mgr_submit(inner.mgr, &inner.ctxpool[buf_idx], (char*)read_buf + buf_idx * MD5_BUF_LEN, MD5_BUF_LEN, hash_flag);
//		}
//	}
//
//	//while (CTX_MGR_FLUSH(inner.mgr));
//	for (int buf_idx = 0; buf_idx < PARALLEL_NUM; buf_idx++) {
//		while (hash_ctx_processing(&inner.ctxpool[buf_idx])) {
//			md5_ctx_mgr_flush(inner.mgr);
//		}
//	}
//	char rst[sizeof(inner.ctxpool[0].job.result_digest) * PARALLEL_NUM * 2 + 4];//extra 4 bytes, only 1 used for \0
//	//memset(rst, 0, sizeof(rst));
//	for (int i = 0; i < PARALLEL_NUM; i++)
//	{
//		for (int j = 0; j < sizeof(inner.ctxpool[i].job.result_digest); j++) {
//			sprintf(rst + i * sizeof(inner.ctxpool[i].job.result_digest)*2 + j * 2, "%02x", *(((char*)&inner.ctxpool[i].job.result_digest) + j));
//		}
//	}
//	rst[sizeof(inner.ctxpool[0].job.result_digest) * PARALLEL_NUM * 2] = 0;
//	S5LOG_DEBUG("Calculate md5 for data(off:0x%lx len:%ld ), A md5:%s", offset, len, rst);
//	return rst;
//}
std::string calc_block_md5(int fd, off_t offset, size_t len)
{
	//len = 256<<10; //for debug only, there may garbage data in object, since trim not enabled
	void* read_buf = memalign(LBA_LENGTH, len);
	if (read_buf == NULL) {
		S5LOG_ERROR("Failed to alloc scrub buf, size:%lld", len);
		return "";
	}
	DeferCall _c([read_buf]() {free(read_buf); });
	InnerScrub inner;

	size_t r = pread(fd, read_buf, len, offset);
	if (r != len) {
		S5LOG_ERROR("Failed read errno:%d", errno);
		return "";
	}

	md5_ctx_mgr_submit(inner.mgr, &inner.ctxpool[0], (char*)read_buf , (uint32_t)len, HASH_ENTIRE);


	while (CTX_MGR_FLUSH(inner.mgr));

	char rst[sizeof(inner.ctxpool[0].job.result_digest) * 2 + 4];//extra 4 bytes, only 1 used for \0
	//memset(rst, 0, sizeof(rst));
	for(int i=0;i< MD5_DIGEST_NWORDS;i++){
		uint32_t word =htobe32(inner.ctxpool[0].job.result_digest[i]);
		sprintf(rst + i * 8, "%08x", word);
	}
		
	rst[sizeof(inner.ctxpool[0].job.result_digest) * PARALLEL_NUM * 2] = 0;
	S5LOG_DEBUG("Calculate md5 for data(off:0x%lx len:%ld ), md5:%s", offset, len, rst);
	return rst;
}