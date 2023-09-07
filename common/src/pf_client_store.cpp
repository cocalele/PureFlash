#include "pf_client_store.h"
int PfClientStore::init(const char* tray_name)
{
	int ret = 0;

	safe_strcpy(this->tray_name, tray_name, sizeof(this->tray_name));
	Cleaner err_clean;
	fd = open(tray_name, O_RDWR | O_DIRECT);
	if (fd == -1) {
		return -errno;
	}
	err_clean.push_back([this]() {::close(fd); });

	/*init ioengine first*/
	//TODO: change io engine according to config file
	ioengine = new PfAioEngine(this);
	//ioengine = new PfIouringEngine(this);  
	ioengine->init();

	if ((ret = read_store_head()) == 0)
	{
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

	trimming_thread = std::thread(&PfFlashStore::trimming_proc, this);

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
			io->complete(MSG_STATUS_INTERNAL);
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
	PfMessageHead* cmd = io->client_iocb->cmd_bd->cmd_bd;
	BufferDescriptor* data_bd = io->client_iocb->data_bd;
	io->opcode = cmd->opcode;
	lmt_key key = { VOLUME_ID(io->rep_id), (int64_t)vol_offset_to_block_slba(cmd->offset, head.objsize_order), 0, 0 };
	auto block_pos = obj_lmt.find(key);
	lmt_entry* entry = NULL;

	if (unlikely(block_pos == obj_lmt.end()))
	{
		S5LOG_DEBUG("Alloc object for rep:0x%llx slba:0x%llx  cmd offset:0x%llx ", io->rep_id, key.slba, cmd->offset);
		int obj_id = client_app_context.rpc_alloc_block(volume, cmd->offset);
		if (obj_id < 0) {
			S5LOG_ERROR("Disk:%s is full!", tray_name);
			TODO: complete IO as error
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
				io->complete(MSG_STATUS_INTERNAL);
				return -EINVAL;
			}

		}
		else if (unlikely(cmd->snap_seq < entry->snap_seq)) {
			S5LOG_ERROR("Write on snapshot not allowed! vol_id:0x%x request snap:%d, target snap:%d",
				cmd->vol_id, cmd->snap_seq, entry->snap_seq);
			io->complete(MSG_STATUS_READONLY);
			return 0;
		}
		else if (unlikely(cmd->snap_seq > entry->snap_seq)) {
			int obj_id = client_app_context.rpc_alloc_block(volume, cmd->offset);
			if (obj_id < 0) {
				S5LOG_ERROR("Disk:%s is full!", tray_name);
				app_context.error_handler->submit_error(io, MSG_STATUS_NOSPACE);
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
			if (spdk_engine_used() && ns->scc)
				begin_cow_scc(&key, entry, cow_entry);
			else
				begin_cow(&key, entry, cow_entry);
			return 0;
		}


	}

	ioengine->submit_io(io, entry->offset + offset_in_block(cmd->offset, in_obj_offset_mask), cmd->length);

	return 0;
}

