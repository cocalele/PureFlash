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

PfVolume::~PfVolume()
{
	for(int i=0;i<shards.size();i++)
	{
		delete shards[i];
		shards[i] = NULL;
	}
}

PfShard::~PfShard()
{
	for(int i=0;i<rep_count;i++)
	{
		delete replicas[i];
		replicas[i] = NULL;
	}
}
