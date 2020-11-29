#include <unistd.h>
#include <string.h>

#include "pf_redolog.h"

#define STORE_META_AUTO_SAVE_INTERVAL 60

int PfRedoLog::init(struct PfFlashStore* s)
{
	int rc = 0;
	int64_t *p;
	this->store = s;
	this->disk_fd = s->fd;
	this->start_offset = s->head.redolog_position;
	this->current_offset = this->start_offset + LBA_LENGTH;
	this->size = s->head.redolog_size;
	this->phase = 1;
	entry_buff = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
	if (entry_buff == NULL)
		return -ENOMEM;
	memset(entry_buff, 0, LBA_LENGTH);
	if (-1 == pwrite(disk_fd, entry_buff, LBA_LENGTH, current_offset)) {
		rc = -errno;
		goto release1;
	}

	p = (int64_t *) entry_buff;
	p[0] = size;
	p[1] = phase; //phase in head is 1, and the first item writen in above has phase=0, so redo log will consider it as obsoleted item
	if (-1 == pwrite(disk_fd, entry_buff, LBA_LENGTH, start_offset)) {
		rc = -errno;
		goto release1;
	}

	return 0;

release1:
	free(entry_buff);
	entry_buff = NULL;
	S5LOG_ERROR("Failed to init redo log, rc:%d", rc);
	return rc;
}

int PfRedoLog::load(struct PfFlashStore* s)
{
	int rc = 0;
	int64_t *p;
	this->store = s;
	this->disk_fd = s->fd;
	this->start_offset = s->head.redolog_position;
	this->size = s->head.redolog_size;
	entry_buff = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
	if (entry_buff == NULL)
		return -ENOMEM;
	memset(entry_buff, 0, LBA_LENGTH);
	this->current_offset = this->start_offset + LBA_LENGTH;


	if (-1 == pread(disk_fd, entry_buff, LBA_LENGTH, start_offset)) {
		rc = -errno;
		goto release1;
	}

	p = (int64_t *) entry_buff;
	phase = p[1] ;
	assert(p[2] == 0xeeeedddd);
	return 0;
release1:
	free(entry_buff);
	entry_buff = NULL;
	S5LOG_ERROR("Failed to load redo log, rc:%d", rc);
	return rc;
}

int PfRedoLog::start()
{
	auto_save_thread = std::thread([this]() {
		while (1)
		{
			if (sleep(STORE_META_AUTO_SAVE_INTERVAL) != 0)
				return 0;
			store->sync_invoke([this]()->int {
				if (current_offset == start_offset + LBA_LENGTH)
					return 0;
				store->save_meta_data();
				return 0;
			});
		}

	});
	return 0;
}
int PfRedoLog::replay()
{
	S5LOG_INFO("Start replay redo log");
	int cnt = 0;
	int rc = 0;
	int64_t offset = start_offset + LBA_LENGTH;
	while(1)
	{
		if (pread(disk_fd, entry_buff, LBA_LENGTH, offset) == -1) {
			S5LOG_ERROR("Failed to read redo log, rc:%d", -errno);
			return -errno;
		}
		PfRedoLog::Item* item = (PfRedoLog::Item*)entry_buff;
		if (item->phase != phase)
			break;
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
	S5LOG_INFO("%s redolog replay finished. %d items replayed", store->tray_name, cnt);
	return 0;
}

int PfRedoLog::discard()
{
	this->phase ++;
	int64_t* p = (int64_t *) entry_buff;
	p[0] = size;
	p[1] = phase; //phase in head is 1, and the first item writen in above has phase=0, so redo log will consider it as obsoleted item
	p[2] = 0xeeeedddd;
	if (-1 == pwrite(disk_fd, entry_buff, LBA_LENGTH, start_offset)) {
		S5LOG_ERROR("Failed to discard redolog, rc:%d", -errno);
		return -errno;
	}

	return 0;
}
int PfRedoLog::log_allocation(const struct lmt_key* key, const struct lmt_entry* entry, int free_list_head)
{
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
	if(pos != store->obj_lmt.end()) {
		store->obj_lmt[e->allocation.bkey] = entry;
	} else {
		store->lmt_entry_pool.free(entry);
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
	if (pwrite(disk_fd, entry_buff, LBA_LENGTH, current_offset) == -1)
	{
		S5LOG_ERROR("Failed to persist redo log, rc:%d", -errno);
		return -errno;
	}
	current_offset += LBA_LENGTH;
	if (current_offset >= start_offset + size)
	{
		return store->save_meta_data();
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
