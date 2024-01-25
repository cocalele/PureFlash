/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include "pf_volume.h"
#include "pf_utils.h"

static const char* status_strs[] = { "OK", "ERROR", "DEGRADED" };
HealthStatus health_status_from_str(const std::string&  status_str)
{
	for(int i=0;i<S5ARRAY_SIZE(status_strs); i++)
	{
		if (status_str == status_strs[i])
			return (HealthStatus)i;
	}
	return (HealthStatus)-1;
}

const char* HealthStatus2Str(HealthStatus code)
{
#define C_NAME(x) case x: return #x;

	static __thread char buf[64];
	switch(code){
		C_NAME(HS_OK)
		C_NAME(HS_ERROR)
		C_NAME(HS_DEGRADED)
		C_NAME(HS_RECOVERYING)
		default:
			sprintf(buf, "%d", code);
			return buf;
	}
}

PfVolume& PfVolume::operator=(PfVolume&& vol)
{
	this->meta_ver = vol.meta_ver;
	this->shard_count = vol.shard_count;
	this->size = vol.size;
	this->snap_seq = vol.snap_seq;
	this->status = vol.status;
	for (int i = 0; i < shard_count; i++) {
		PfShard* s = shards[i];
		for (int j = 0; j < s->rep_count; j++) {
			if (s->replicas[j] == NULL) {
				continue;
			}
			if (s->replicas[j]->status == HealthStatus::HS_RECOVERYING && vol.shards[i]->replicas[j]->status == HealthStatus::HS_ERROR) {
				vol.shards[i]->replicas[j]->status = HealthStatus::HS_RECOVERYING; //keep recoverying continue
			}
		}

		this->shards[i] = vol.shards[i];
		vol.shards[i] = NULL;
		delete s;
	}

	for (int i = shard_count; i < vol.shards.size(); i++) { //enlarged shard
		shards.push_back(vol.shards[i]);
		vol.shards[i] = NULL;
	}
	return *this;
}

PfVolume::~PfVolume()
{
	S5LOG_DEBUG("Desctruct PfVolume, %d shards", shards.size());
	for(int i=0;i<shards.size();i++)
	{
		delete shards[i];
		shards[i] = NULL;
	}
}

PfShard::~PfShard()
{
	S5LOG_DEBUG("Desctruct PfShard, idx:%d", this->shard_index);
	for(int i=0;i< MAX_REP_COUNT; i++)
	{
		if (replicas[i] == NULL) {
			continue;
		}
		delete replicas[i];
		replicas[i] = NULL;
	}
}
