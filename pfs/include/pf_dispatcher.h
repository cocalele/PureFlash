#ifndef pf_dispatcher_h__
#define pf_dispatcher_h__

#include "pf_volume.h"
#include <map>
class S5Dispatcher
{
public:
	std::map<uint64_t, S5Volume*> opened_volumes;

	int prepare_volume(S5Volume* vol);
	int dispatch_io();
};
#endif // pf_dispatcher_h__
