#include "pf_dispatcher.h"
int PfDispatcher::prepare_volume(PfVolume* vol)
{
	if (opened_volumes.find(vol->id) != opened_volumes.end())
	{
		delete vol;
		return -EALREADY;
	}
	opened_volumes[vol->id] = vol;
	return 0;
}
