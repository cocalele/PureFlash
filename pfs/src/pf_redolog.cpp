/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <unistd.h>
#include <sys/prctl.h>
#include <string.h>

#include "pf_redolog.h"
#include "pf_app_ctx.h"
#include "pf_main.h"
#include "pf_spdk_engine.h"

#define STORE_META_AUTO_SAVE_INTERVAL (120)

int PfRedoLog::init(struct PfFlashStore* s)
{
	this->store = s;
	this->disk_fd = s->fd;
	this->size = s->head.redolog_size;
	entry_buff = align_malloc_spdk(LBA_LENGTH, LBA_LENGTH, NULL);
	if (entry_buff == NULL)
		return -ENOMEM;
	memset(entry_buff, 0, LBA_LENGTH);

	return 0;
}

int PfRedoLog::set_log_phase(int64_t _phase, uint64_t offset)
{
	phase = _phase;
	start_offset = offset;
	current_offset = start_offset;

	return 0;
}

int PfRedoLog::start()
{
	auto_save_thread = std::thread([this]() {
		int rc;
		struct timespec timeout;
		struct timeval now;
		char name[256] = {0};
		sprintf(name, "md_%s", store->tray_name);
		prctl(PR_SET_NAME, name);
		if (app_context.engine == SPDK)
			((PfspdkEngine*)store->ioengine)->pf_spdk_io_channel_open(1);
		while (1)
		{
			if (0 != gettimeofday(&now, NULL)) {
				S5LOG_ERROR("failed to gettimeofday");
				continue;
			}
			timeout.tv_sec = now.tv_sec + STORE_META_AUTO_SAVE_INTERVAL;
			timeout.tv_nsec = now.tv_usec * 1000;
			int done = 0;
			pthread_mutex_lock(&store->md_lock);
			while (1) {
				S5LOG_INFO("compaction:now state is %d", store->to_run_compact.load());
				switch (store->to_run_compact.load())
				{
					case COMPACT_TODO:
					{
						store->to_run_compact.store(COMPACT_RUNNING);
						rc = store->compact_meta_data();
						if (rc) {
							S5LOG_ERROR("failed to compact metadata");
							store->to_run_compact.store(COMPACT_ERROR);
							store->event_queue->post_event(EVT_SAVEMD, 0, NULL, NULL);
						} else {
							store->to_run_compact.store(COMPACT_IDLE);
						}
						done = 1;
					}
					break;
					case COMPACT_IDLE:
					case COMPACT_STOP:
					case COMPACT_ERROR:
					{
						rc = pthread_cond_timedwait(&store->md_cond, &store->md_lock, &timeout);
						if (store->to_run_compact.load() == COMPACT_STOP) {
							S5LOG_ERROR("compaction is skip");
							done = 1;
							break;
						}
						if (rc == 0) {
							S5LOG_INFO("metadata compatcion thread waked up, state:%d", store->to_run_compact.load());
						}else if (rc == ETIMEDOUT) {
							store->event_queue->post_event(EVT_SAVEMD, 0, NULL, NULL);
							done = 1;
						}else {
							S5LOG_ERROR("cond wait get error rc:%d\n", rc);
							done = 1;
						}
					}
					break;
				}
				if (done == 1)
					break;
			}
			pthread_mutex_unlock(&store->md_lock);
		}
		if (app_context.engine == SPDK)
			((PfspdkEngine *)store->ioengine)->pf_spdk_io_channel_close(NULL);
	});
	return 0;
}
int PfRedoLog::replay(int64_t start_phase, int which)
{
	int cnt = 0;
	int rc = 0;
	int64_t offset = store->get_meta_position(REDOLOG, which);
	S5LOG_INFO("Start replay redo log at %s log zone, start_phase:%d, offset:0x%lx",
		store->meta_positon_2str(REDOLOG, which == CURRENT ? store->head.current_redolog : store->oppsite_redolog_zone()), 
		start_phase, offset);
	while(1)
	{
		if (store->ioengine->sync_read(entry_buff, LBA_LENGTH, offset) == -1) {
			S5LOG_ERROR("Failed to read redo log, rc:%d", -errno);
			return -errno;
		}
		PfRedoLog::Item* item = (PfRedoLog::Item*)entry_buff;
		if (item->phase != start_phase)
			break;
		S5LOG_DEBUG("replay redo log item type:%d", item->type);

		switch (item->type)
		{
			case ItemType::ALLOCATE_OBJ:
				rc = redo_allocation(item);
				break;
			case ItemType::FREE_OBJ:
				rc = redo_free(item);
				break;
			case ItemType::TRIM_OBJ:
				rc = redo_trim(item);
				break;
			case ItemType::SNAP_SEQ_CHANGE:
				rc=redo_snap_seq_change(item);
				break;
			case ItemType::STATUS_CHANGE:
				rc = redo_state_change(item);
				break;
			default:
				S5LOG_FATAL("Unknown redo log type:%d", item->type);
				rc = -1;
		}
		if (rc)
		{
			S5LOG_ERROR("Failed to replay log, ssd:%s, entry at:%lx", store->tray_name, offset);
			return rc;
		}
		cnt++;
		offset += LBA_LENGTH;

	}
	S5LOG_INFO("%s log zone:%s redolog replay finished. %d items replayed", store->tray_name, store->meta_positon_2str(REDOLOG, which), cnt);
	return 0;
}

int PfRedoLog::discard()
{
	store->head.redolog_phase++;
	store->head.current_redolog = (uint8_t)store->oppsite_redolog_zone();

	return 0;
}
int PfRedoLog::log_allocation(const struct lmt_key* key, const struct lmt_entry* entry, int free_list_head)
{
	//S5LOG_DEBUG("log allocation for key:%s", key->to_string().c_str());

	PfRedoLog::Item *item = (PfRedoLog::Item*)entry_buff;
	*item = PfRedoLog::Item{phase:phase, type:ItemType::ALLOCATE_OBJ, {*key, *entry, free_list_head } };
	return write_entry();
}
int PfRedoLog::log_free(int block_id, int trim_queue_head, int free_queue_tail)
{
	PfRedoLog::Item *item = (PfRedoLog::Item*)entry_buff;
	*item = PfRedoLog::Item{phase:phase, type:ItemType::FREE_OBJ };
	item->free.obj_id = block_id;
	item->free.trim_queue_head = trim_queue_head;
	item->free.free_queue_tail = free_queue_tail;
	return write_entry();
}
int PfRedoLog::log_trim(const struct lmt_key* key, const struct lmt_entry* entry, int trim_list_tail)
{
	PfRedoLog::Item *item = (PfRedoLog::Item*)entry_buff;
	*item = PfRedoLog::Item{phase:phase, type:ItemType::TRIM_OBJ, {*key, *entry, trim_list_tail } };
	return write_entry();
}
int PfRedoLog::redo_allocation(Item* e)
{
	struct lmt_entry* entry = store->lmt_entry_pool.alloc();
	if (entry == NULL)
	{
		return -ENOMEM;
	}
	*entry = e->allocation.bentry;
	entry->init_for_redo();
	auto pos = store->obj_lmt.find(e->allocation.bkey);
	if(pos == store->obj_lmt.end()) {
		store->obj_lmt[e->allocation.bkey] = entry;
	} else {
		int rc = insert_lmt_entry_list(&pos->second, entry, [entry](lmt_entry* item)->bool{
				return item->snap_seq > entry->snap_seq;
			},
			[entry](lmt_entry* item)->bool {
				return item->snap_seq == entry->snap_seq;
			}
		);
		if(rc == -EEXIST){
			//duplicated entry, free it
			store->lmt_entry_pool.free(entry);
		}
	}
	store->free_obj_queue.head = e->allocation.free_queue_head;
	return 0;
}
int PfRedoLog::redo_trim(Item* e)
{
	store->trim_obj_queue.tail = e->trim.trim_queue_tail;
	int tail = e->trim.trim_queue_tail - 1;
	if (tail < 0)
		tail = store->trim_obj_queue.queue_depth - 1;
	store->trim_obj_queue.data[tail] = (int32_t)store->offset_to_obj_id(e->trim.bentry.offset);

	auto pos = store->obj_lmt.find(e->trim.bkey);
	if(pos == store->obj_lmt.end())
		return 0;
	delete_matched_entry(&pos->second,
	                     [e](struct lmt_entry* _entry)->bool {
		                     return _entry->offset == e->trim.bentry.offset;
	                     },
	                     [this](struct lmt_entry* _entry)->void {
		                     store->lmt_entry_pool.free(_entry);
	                     });
	if(pos->second == NULL)
		store->obj_lmt.erase(pos);
	return 0;

}
int PfRedoLog::redo_free(Item* e)
{
	store->free_obj_queue.tail = e->free.free_queue_tail;
	int tail = e->free.free_queue_tail - 1;
	if (tail < 0)
		tail = store->free_obj_queue.queue_depth - 1;
	store->free_obj_queue.data[tail] = e->free.obj_id;

	store->trim_obj_queue.head = e->free.trim_queue_head;

	return 0;
}

int PfRedoLog::write_entry()
{
	//S5LOG_INFO("record log at phase:%d offset:0x%lx", phase, current_offset);
	if (store->ioengine->sync_write(entry_buff, LBA_LENGTH, current_offset) == -1)
	{
		S5LOG_ERROR("Failed to persist redo log, rc:%d", -errno);
		return -errno;
	}
	current_offset += LBA_LENGTH;
	if (current_offset >= start_offset + size)
	{
		return store->meta_data_compaction_trigger(COMPACT_TODO, true);
	}
	return 0;
}

int PfRedoLog::stop()
{
	pthread_cancel(auto_save_thread.native_handle());
	auto_save_thread.join();
	return 0;
}

int PfRedoLog::log_snap_seq_change(const struct lmt_key* key, const struct lmt_entry* entry, int old_seq)
{
	PfRedoLog::Item *item = (PfRedoLog::Item*)entry_buff;
	item->phase = phase;
	item->type = ItemType::SNAP_SEQ_CHANGE;
	item->snap_seq_change.bkey = *key;
	item->snap_seq_change.bentry = *entry;
	item->snap_seq_change.old_snap_seq = old_seq;
	return write_entry();

}

int PfRedoLog::redo_snap_seq_change(PfRedoLog::Item* e)
{
	auto pos = store->obj_lmt.find(e->snap_seq_change.bkey);
	if (pos != store->obj_lmt.end())
	{
		for (struct lmt_entry* h = pos->second; h != NULL; h = h->prev_snap)
		{
			if (h->offset == e->snap_seq_change.bentry.offset)
			{
				if (h->snap_seq == e->snap_seq_change.old_snap_seq)
					h->snap_seq = e->snap_seq_change.bentry.snap_seq;
				break;
			}
		}
	}
	return 0;
}

int PfRedoLog::log_status_change(const lmt_key* key, const lmt_entry* entry, EntryStatus old_status)
{
	PfRedoLog::Item* item = (PfRedoLog::Item*)entry_buff;
	item->phase = phase;
	item->type = ItemType::STATUS_CHANGE;
	item->state_change.bkey = *key;
	item->state_change.bentry = *entry;
	item->state_change.old_status = old_status;
	return write_entry();
}

int PfRedoLog::redo_state_change(PfRedoLog::Item* e)
{
	auto pos = store->obj_lmt.find(e->snap_seq_change.bkey);
	if (pos != store->obj_lmt.end())
	{
		for (struct lmt_entry* h = pos->second; h != NULL; h = h->prev_snap)
		{
			if (h->offset == e->state_change.bentry.offset)
			{
				if (h->status == e->state_change.old_status)
					h->status = e->state_change.bentry.status;
				break;
			}
		}
	}
	return 0;
}
