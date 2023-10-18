#include "pf_app_ctx.h"

// g_app_ctx has global info, such as rdma device info
PfAppCtx* g_app_ctx=NULL;

bool spdk_engine = false;

bool spdk_engine_used()
{
	return spdk_engine == true;
}

void spdk_engine_set(bool use_spdk)
{
	spdk_engine = use_spdk;
}
