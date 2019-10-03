#include "s5_redolog.h"
int S5RedoLog::init(struct S5FlashStore* s)
{
	int rc = 0;
	int64_t *p;
	this->store = s;
	fd = s->dev_fd;
	this->start_offset = s->head.redo_log_position;
	this->current_offset = this->start_offset + PAGE_SIZE;
	this->size = s->head.redo_log_size;
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
	auto_save_thread = std::thread([]() {
		while (1)
		{
			if (sleep(STORE_META_AUTO_SAVE_INTERVAL) != 0)
				return NULL;
			store->receiver.sync_lambda_call([store]()->int {
				if (store->redo_log.current_offset == store->redo_log.start_offset + PAGE_SIZE)
					return 0;
				store->save_metadata();
				return 0;
			});
		}

	});

	return 0;
release2:
	pthread_cancel(meta_data_write_thread);
	pthread_join(meta_data_write_thread, NULL);
release1:
	neon_free(entry_buff);
	golog(NEON_LOG_ERROR, "Failed to init redo log, rc:%d", rc);
	return rc;
}
int S5RedoLog::load(struct qfa_ssd_info* ssd);
int S5RedoLog::replay();
int S5RedoLog::discard();
int S5RedoLog::log_allocation(const struct block_key* key, const struct block_entry* entry, int free_list_head);
int S5RedoLog::log_free(int block_id, int trim_list_head, int free_list_tail);
int S5RedoLog::log_trim(const struct block_key* key, const struct block_entry* entry, int trim_list_tail);
int S5RedoLog::redo_allocation(LogEntry* e);
int S5RedoLog::redo_trim(LogEntry* e);
int S5RedoLog::redo_free(LogEntry* e);

private:
	int write_entry();

