#include "pf_app_ctx.h"

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
