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
	this->phase = 0;//will be increased to 1 on the followed discard() call
	entry_buff = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
	if (entry_buff == NULL)
		return -ENOMEM;
	memset(entry_buff, 0, LBA_LENGTH);
	if (-1 == pwrite(disk_fd, entry_buff, LBA_LENGTH, current_offset))
	{
		rc = -errno;
		goto release1;
	}

	p = (int64_t*)entry_buff;
	p[0] = size;
	p[1] = phase;
	if (-1 == pwrite(disk_fd, entry_buff, LBA_LENGTH, start_offset))
	{
		rc = -errno;
		goto release1;
	}
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

release1:
	free(entry_buff);
	entry_buff = NULL;
	S5LOG_ERROR("Failed to init redo log, rc:%d", rc);
	return rc;
}
int PfRedoLog::load()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::replay()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::discard()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::log_allocation(const struct lmt_key* key, const struct lmt_entry* entry, int free_list_head)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::log_free(int block_id, int trim_list_head, int free_list_tail)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::log_trim(const struct lmt_key* key, const struct lmt_entry* entry, int trim_list_tail)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::redo_allocation(Item* e)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::redo_trim(Item* e)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int PfRedoLog::redo_free(Item* e)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}

int PfRedoLog::write_entry()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}

int PfRedoLog::stop()
{
	pthread_cancel(auto_save_thread.native_handle());
	auto_save_thread.join();
	return 0;
}
