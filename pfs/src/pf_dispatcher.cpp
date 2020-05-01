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
int PfDispatcher::process_event(int event_type, int arg_i, void* arg_p)
{
	switch(event_type) {
	case EVT_IO_REQ:
		
}
