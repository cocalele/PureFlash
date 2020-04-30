#ifndef pf_dispatcher_h__
#define pf_dispatcher_h__

#include "pf_volume.h"
#include <map>
class PfDispatcher
{
public:
	std::map<uint64_t, PfVolume*> opened_volumes;

	int prepare_volume(PfVolume* vol);
	int dispatch_io();
};
#endif // pf_dispatcher_h__
