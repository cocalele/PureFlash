#include <unistd.h>

#include "pf_message.h"
#include "pf_utils.h"
#include "pf_volume_type.h"
#include "pf_client_store.h"
#include "pf_client_priv.h"
#include "pf_app_ctx.h"
/*
 * A PfClientStore serve to only one PfClientVolume.
 * If multiple volume use same shared disk, more than one PfClientStore instances will be created.
 * multiple PfClientStore implies multiple IoEngines, so each volume have an individual IO channel
 */
int PfClientStore::init(PfClientVolume* vol, const char* dev_name, const char* dev_uuid)
{
	int ret = 0;
	this->volume = vol;
	safe_strcpy(this->tray_name, dev_name, sizeof(this->tray_name));
	Cleaner err_clean;
	fd = ::open(tray_name, O_RDWR | O_DIRECT);
	if (fd == -1) {
		return -errno;
	}
	err_clean.push_back([this]() {::close(fd); });

	/*init ioengine first*/
	//TODO: change io engine according to config file
	ioengine = new PfAioEngine(tray_name, fd, volume->runtime_ctx);
	err_clean.push_back([this]() {delete ioengine; });
	//ioengine = new PfIouringEngine(this);
	ret = ioengine->init();
	if(ret){
		S5LOG_ERROR("Failed to init ioengine, rc:%d", ret);
		return ret;
	}

	if ((ret = read_store_head()) == 0)
	{
		if(strcmp(uuid_str, dev_uuid) != 0){
			S5LOG_ERROR("UUID on dev:%s %s not same as required:%s", dev_name, uuid_str, dev_uuid);
			return -EINVAL;
		}
		int obj_count = (int)((head.tray_capacity - head.meta_size) >> head.objsize_order);

		ret = lmt_entry_pool.init(obj_count * 2);
		if (ret) {
			S5LOG_ERROR("Failed to init lmt_entry_pool, disk:%s rc:%d", tray_name, ret);
			return ret;
		}

		obj_lmt.reserve(obj_count * 2);
	}
	else
		return ret;

	in_obj_offset_mask = head.objsize - 1;

	zk_watch_thread = std::thread(&PfClientStore::zk_watch_proc, this);

	err_clean.cancel_all();
	return ret;
}

int PfClientStore::do_read(IoSubTask* io)
{
	PfMessageHead* cmd = io->parent_iocb->cmd_bd->cmd_bd;

	lmt_key key = { VOLUME_ID(io->rep_id), (int64_t)vol_offset_to_block_slba(cmd->offset, head.objsize_order), 0, 0 };
	auto block_pos = obj_lmt.find(key);
	lmt_entry* entry = NULL;
	if (block_pos != obj_lmt.end())
		entry = block_pos->second;
	while (entry && cmd->snap_seq < entry->snap_seq)
		entry = entry->prev_snap;
	if (entry == NULL)
	{
		io->complete_read_with_zero();
	}
	else
	{
		if (likely(entry->status == EntryStatus::NORMAL || entry->status == EntryStatus::DELAY_DELETE_AFTER_COW)) {
			//TODO: should we lock this entry first?
			io->opcode = S5_OP_READ;
			ioengine->submit_io(io, entry->offset + offset_in_block(cmd->offset, in_obj_offset_mask), cmd->length);

		}
		else
		{
			S5LOG_ERROR("Read on object in unexpected state:%d", entry->status);
			io->ops->complete(io, MSG_STATUS_INTERNAL);
		}

	}
	return 0;
}

/**
 * write data to flash store.
 * a LBA is a block of data 4096 bytes.
 * @return number of lbas has written to store
 *         negative value for error
 */
int PfClientStore::do_write(IoSubTask* io)
{
	PfMessageHead* cmd = io->parent_iocb->cmd_bd->cmd_bd;
	//BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	io->opcode = cmd->opcode;
	lmt_key key = { VOLUME_ID(io->rep_id), (int64_t)vol_offset_to_block_slba(cmd->offset, head.objsize_order), 0, 0 };
	auto block_pos = obj_lmt.find(key);
	lmt_entry* entry = NULL;

	if (unlikely(block_pos == obj_lmt.end()))
	{
		S5LOG_DEBUG("Alloc object for rep:0x%llx slba:0x%llx  cmd offset:0x%llx ", io->rep_id, key.slba, cmd->offset);
		int obj_id = volume->runtime_ctx->rpc_alloc_block(volume, cmd->offset);
		if (obj_id < 0) {
			S5LOG_ERROR("Disk:%s is full!", tray_name);
			PfClientIocb *parent_io = (PfClientIocb*)io->parent_iocb;
			parent_io->ulp_handler(parent_io->ulp_arg, -ENOSPC);
			return 0;
		}
		entry = lmt_entry_pool.alloc();
		*entry = lmt_entry{ offset: obj_id_to_offset(obj_id),
			snap_seq :  volume->snap_seq,
			status : EntryStatus::NORMAL,
			prev_snap : NULL,
			waiting_io : NULL
		};
		obj_lmt[key] = entry;
	}
	else
	{
		entry = block_pos->second;
		if (unlikely(entry->status == EntryStatus::RECOVERYING)) {
			S5LOG_FATAL("Shared disk should not in RECOVERYING state");
		}
		if (likely(cmd->snap_seq == entry->snap_seq)) {
			if (unlikely(entry->status != EntryStatus::NORMAL))
			{
				if (entry->status == EntryStatus::COPYING) {
					io->next = entry->waiting_io;
					entry->waiting_io = io; //insert io to waiting list
					return 0;
				}
				S5LOG_FATAL("Block in abnormal status:%d", entry->status);
				io->ops->complete(io, MSG_STATUS_INTERNAL);
				return -EINVAL;
			}

		}
		else if (unlikely(cmd->snap_seq < entry->snap_seq)) {
			S5LOG_ERROR("Write on snapshot not allowed! vol_id:0x%x request snap:%d, target snap:%d",
				cmd->vol_id, cmd->snap_seq, entry->snap_seq);
			io->ops->complete(io, MSG_STATUS_READONLY);
			return 0;
		}
		else if (unlikely(cmd->snap_seq > entry->snap_seq)) {
			int obj_id = volume->runtime_ctx->rpc_alloc_block(volume, cmd->offset);
			if (obj_id < 0) {
				S5LOG_ERROR("Disk:%s is full!", tray_name);
				io->ops->complete(io, MSG_STATUS_READONLY);
				return 0;
			}
			struct lmt_entry* cow_entry = lmt_entry_pool.alloc();
			*cow_entry = lmt_entry{ offset: obj_id_to_offset(obj_id),
					snap_seq : cmd->snap_seq,
					status : EntryStatus::COPYING,
					prev_snap : entry,
					waiting_io : NULL
			};
			obj_lmt[key] = cow_entry;
			
			io->next = cow_entry->waiting_io;
			cow_entry->waiting_io = io; //insert io to waiting list
			S5LOG_DEBUG("Call begin_cow for io:%p, src_entry:%p cow_entry:%p", io, entry, cow_entry);
			//TODO: spdk support, like that in server side
			begin_cow(&key, entry, cow_entry);
			return 0;
		}


	}

	ioengine->submit_io(io, entry->offset + offset_in_block(cmd->offset, in_obj_offset_mask), cmd->length);

	return 0;
}

void PfClientStore::do_cow_entry(lmt_key* key, lmt_entry* srcEntry, lmt_entry* dstEntry)
{//this function called in thread pool, not the store's event thread
	CowTask r;
	r.src_offset = srcEntry->offset;
	r.dst_offset = dstEntry->offset;
	r.size = COW_OBJ_SIZE;
	sem_init(&r.sem, 0, 0);
	S5LOG_INFO("begin do_cow_entry");
	r.buf = g_app_ctx->cow_buf_pool.alloc(COW_OBJ_SIZE, spdk_engine_used());
	ioengine->submit_cow_io(&r, r.src_offset, r.size);

	sem_wait(&r.sem);
	if (unlikely(r.complete_status != PfMessageStatus::MSG_STATUS_SUCCESS)) {
		S5LOG_ERROR("COW read failed, status:%d", r.complete_status);
		goto cowfail;
	}

	ioengine->submit_cow_io(&r, r.dst_offset, r.size);
	sem_wait(&r.sem);
	if (unlikely(r.complete_status != PfMessageStatus::MSG_STATUS_SUCCESS)) {
		S5LOG_ERROR("COW write failed, status:%d", r.complete_status);
		goto cowfail;
	}
	S5LOG_INFO("end do_cow_entry");
	volume->event_queue->sync_invoke([key, srcEntry, dstEntry, this]()->int {
		dstEntry->status = EntryStatus::NORMAL;
		IoSubTask* t = dstEntry->waiting_io;
		dstEntry->waiting_io = NULL;
		while (t) {
			if (unlikely(t->opcode != S5_OP_WRITE && t->opcode != S5_OP_REPLICATE_WRITE)) {
				S5LOG_FATAL("Unexcepted op code:%d", t->opcode);
			}

			do_write(t);
			t = t->next;
		}
		if (srcEntry->status == DELAY_DELETE_AFTER_COW) {
			delete_obj(key, srcEntry);
		}
		return 0;
		});
	g_app_ctx->cow_buf_pool.free(r.buf, spdk_engine_used());
	return;
cowfail:
	volume->event_queue->sync_invoke([dstEntry]()->int
		{
			IoSubTask* t = dstEntry->waiting_io;
			dstEntry->waiting_io = NULL;
			while (t) {
				S5LOG_ERROR("return REOPEN for cowfail, cid:%d", t->parent_iocb->cmd_bd->cmd_bd->command_id);
				t->ops->complete(t, PfMessageStatus::MSG_STATUS_REOPEN);
				t = t->next;
			}
			return 0;
		});
	g_app_ctx->cow_buf_pool.free(r.buf, spdk_engine_used());
	return;
}

void PfClientStore::zk_watch_proc()
{

	int rc = 0;
	do {
		rc = volume->runtime_ctx->zk_client.watch_disk_owner(uuid_str, [this](const char* new_owner) {
			if(strlen(new_owner) == 0){
				S5LOG_ERROR("No owner ready for disk:%s", tray_name);
			}
			safe_strcpy(volume->owner_ip, new_owner, sizeof(volume->owner_ip));
		});
	}while(rc != -EINTR); //only terminate on thread cancel
	S5LOG_INFO("Stop watching ssd:%s", tray_name);
}

PfClientStore::~PfClientStore()
{
	S5LOG_DEBUG("PfFlashStore:%p destrutor", this);
	pthread_cancel(zk_watch_thread.native_handle());
	zk_watch_thread.join();
	delete ioengine;
	::close(fd);
	ioengine = NULL;
	fd=-1;
}

int PfClientStore::read_store_head()
{
	void* buf = align_malloc_spdk(LBA_LENGTH, LBA_LENGTH, NULL);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in read_store_head");
		return -ENOMEM;
	}
	DeferCall _rel([buf, this]() {
		free_spdk(buf);
		});

	if (LBA_LENGTH != ioengine->sync_read(buf, LBA_LENGTH, 0))
		return -errno;
	memcpy(&head, buf, sizeof(head));
	if (head.magic != 0x3553424e) //magic number, NBS5
		return -EUCLEAN;
	if (head.version != S5_VERSION) //S5 version
		return -EUCLEAN;
	uuid_unparse(head.uuid, uuid_str);
	S5LOG_INFO("Load disk:%s, uuid:%s", tray_name, uuid_str);
	return 0;
}

int PfClientStore::delete_obj(struct lmt_key* key, struct lmt_entry* entry)
{
	auto pos = obj_lmt.find(*key);
	if (pos == obj_lmt.end())
		return 0;
	delete_matched_entry(&pos->second,
		[entry](struct lmt_entry* _entry)->bool {
			return _entry == entry;
		},
		[key, this](struct lmt_entry* _entry)->void {
			lmt_entry_pool.free(_entry);
		});
	if (pos->second == NULL)
		obj_lmt.erase(pos);
	return volume->runtime_ctx->rpc_delete_obj(volume, key->slba, entry->snap_seq);
}

