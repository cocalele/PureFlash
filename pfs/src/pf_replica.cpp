/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include "pf_replica.h"
#include "pf_flash_store.h"

int PfLocalReplica::submit_io(IoSubTask* subtask)
{
	return disk->event_queue->post_event(EVT_IO_REQ, 0, subtask);
}

int PfSyncRemoteReplica::submit_io(IoSubTask* subtask)
{
	return replicator->event_queue->post_event(EVT_IO_REQ, 0, subtask);
}