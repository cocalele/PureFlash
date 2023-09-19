#include "pf_app_ctx.h"

PfAppCtx* g_app_ctx=NULL;

bool spdk_engine = false;



void spdk_engine_set(bool use_spdk)
{
	spdk_engine = use_spdk;
}
