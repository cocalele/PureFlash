#include "pf_replica.h"
#include "pf_flash_store.h"

int S5LocalReplica::submit_io(IoSubTask* subtask)
{
	return store->event_queue.post_event(EVT_IO_REQ, 0, subtask);
}