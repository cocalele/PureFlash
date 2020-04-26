#include "pf_dispatcher.h"
int S5Dispatcher::prepare_volume(S5Volume* vol)
{
	if (opened_volumes.find(vol->id) != opened_volumes.end())
	{
		delete vol;
		return -EALREADY;
	}
	opened_volumes[vol->id] = vol;
	return 0;
}
