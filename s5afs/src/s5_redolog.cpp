#include <unistd.h>
#include <string.h>

#include "s5_redolog.h"

#define STORE_META_AUTO_SAVE_INTERVAL 60

int S5RedoLog::init(struct S5FlashStore* s)
{
	int rc = 0;
	int64_t *p;
	this->store = s;
	fd = s->dev_fd;
	this->start_offset = s->head.redolog_position;
	this->current_offset = this->start_offset + PAGE_SIZE;
	this->size = s->head.redolog_size;
	this->phase = 0;//will be increased to 1 on the followed discard() call
	entry_buff = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
	if (entry_buff == NULL)
		return -ENOMEM;
	memset(entry_buff, 0, PAGE_SIZE);
	if (-1 == pwrite(fd, entry_buff, PAGE_SIZE, current_offset))
	{
		rc = -errno;
		goto release1;
	}

	p = (int64_t*)entry_buff;
	p[0] = size;
	p[1] = phase;
	if (-1 == pwrite(fd, entry_buff, PAGE_SIZE, start_offset))
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
				if (current_offset == start_offset + PAGE_SIZE)
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
int S5RedoLog::load()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::replay()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::discard()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::log_allocation(const struct block_key* key, const struct block_entry* entry, int free_list_head)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::log_free(int block_id, int trim_list_head, int free_list_tail)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::log_trim(const struct block_key* key, const struct block_entry* entry, int trim_list_tail)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::redo_allocation(Item* e)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::redo_trim(Item* e)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}
int S5RedoLog::redo_free(Item* e)
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}

int S5RedoLog::write_entry()
{
	S5LOG_FATAL("%s not implemented", __FUNCTION__);
	return 0;
}

int S5RedoLog::stop()
{
	pthread_cancel(auto_save_thread.native_handle());
	auto_save_thread.join();
	return 0;
}
