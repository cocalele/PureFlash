#ifndef s5_dispatcher_h__
#define s5_dispatcher_h__

#include "afs_volume.h"
#include <map>
class S5Dispatcher
{
public:
	std::map<uint64_t, S5Volume*> opened_volumes;

	int prepare_volume(S5Volume* vol);
	int dispatch_io();
};
#endif // s5_dispatcher_h__
