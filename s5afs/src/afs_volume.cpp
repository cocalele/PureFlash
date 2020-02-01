#include "afs_volume.h"
static const char* status_strs[] = { "OK", "ERROR", "DEGRADED" };
HealthStatus health_status_from_str(const std::string&  status_str)
{
	for(int i=0;i<ARRAY_SIZE(status_strs); i++)
	{
		if (status_str == status_strs[i])
			return (HealthStatus)i;
	}
	return (HealthStatus)-1;
}

S5Volume::~S5Volume()
{
	for(int i=0;i<shards.size();i++)
	{
		delete shards[i];
		shards[i] = NULL;
	}
}

S5Shard::~S5Shard()
{
	for(int i=0;i<rep_count;i++)
	{
		delete replicas[i];
		replicas[i] = NULL;
	}
}
