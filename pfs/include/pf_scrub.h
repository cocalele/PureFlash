/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//
// Created by liu_l on 12/26/2020.
//

#ifndef PUREFLASH_PF_SCRUB_H
#define PUREFLASH_PF_SCRUB_H

#include <stdlib.h>
#include "isa-l_crypto.h"
#include "pf_volume.h"
#include "pf_restful_api.h"

#define DIGEST_NWORDS   MD5_DIGEST_NWORDS
#define MB_BUFS         MD5_MAX_LANES
#define HASH_CTX_MGR    MD5_HASH_CTX_MGR
#define HASH_CTX	MD5_HASH_CTX

#define OSSL_THREAD_FUNC	md5_ossl_func
#define OSSL_HASH_FUNC		MD5
#define MB_THREAD_FUNC		md5_mb_func
#define CTX_MGR_INIT		md5_ctx_mgr_init
#define CTX_MGR_SUBMIT		md5_ctx_mgr_submit
#define CTX_MGR_FLUSH		md5_ctx_mgr_flush
#define rounds_buf MD5_MAX_LANES

class Scrub {
public:
	Scrub() noexcept;
	~Scrub();
	int feed_data(void* buf, size_t len, size_t off);
	std::string cal_replica(PfFlashStore* s, replica_id_t rep_id);
	static int cal_object(PfFlashStore* s, replica_id_t rep_id, int64_t obj_idx, std::list<SnapshotMd5 >& rst);
private:
	HASH_CTX_MGR *mgr = NULL;
	HASH_CTX *ctxpool = NULL, *ctx = NULL;
};


#endif //PUREFLASH_PF_SCRUB_H
