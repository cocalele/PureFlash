#include "pf_app_ctx.h"

// g_app_ctx has global info, such as rdma device info
PfAppCtx* g_app_ctx=NULL;

bool spdk_engine = false;



void spdk_engine_set(bool use_spdk)
{
	spdk_engine = use_spdk;
}
