/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
/**
 * Copyright (C), 2019.
 * @endcode GBK
 *
 * flash_store ����һ���洢��Ԫ������һ��SSD����flash����flash_store�����Ĺ��ܰ�����
 *  1. ��ʼ��Store�� ���һ��SSD�Ǹɾ���δ����ʼ���ģ���ô�Ͷ����ʼ���������������������ݣ��ͼ���ǲ���һ��
 *     �Ϸ���Store������Ǿͽ���Load���̡�
 *  2. ע��store, ��store��Ϣע�ᵽzookeeper����
 *  3. ����IO����Dispatcher�ὫIO���͵�Store�̣߳�Store�߳���libaio�ӿڽ�IO�·���
 *  4. polling IO���·���SSD����flash����IO��ɺ���aio poller�̴߳���
 */


#include <unistd.h>
#include <errno.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <malloc.h>
#include <string.h>
#include <libaio.h>
#include <thread>
#include <sys/prctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>

#include "pf_iotask.h"
#include "pf_flash_store.h"
#include "pf_utils.h"
#include "pf_server.h"
#include "pf_log.h"
#include "pf_message.h"
#include "pf_cluster.h"
#include "pf_md5.h"
#include "pf_redolog.h"
#include "pf_block_tray.h"
#include "pf_dispatcher.h"
#include <nlohmann/json.hpp>
#include "pf_restful_api.h"
#include "pf_client_priv.h"
#include "pf_spdk.h"
#include "pf_spdk_engine.h"
#include "pf_atslock.h"
#include "pf_trace_defs.h"
#include "spdk/trace.h"
#include "spdk/env.h"

using namespace std;
using nlohmann::json;


static int clean_meta_area(PfIoEngine *eng, size_t size)
{
	size_t buf_len = 1 << 20;
	void *buf = align_malloc_spdk(LBA_LENGTH, buf_len, NULL);
	for(off_t off = 0; off < size; off += buf_len) {
		if(eng->sync_write(buf, buf_len, off) != buf_len){
			S5LOG_ERROR("Failed write zero to meta area, rc:%d", errno);
			free_spdk(buf);
			return -errno;
		}
	}

	free_spdk(buf);
	return 0;
}

int  PfFlashStore::format_disk()
{
	int ret=0;
	S5LOG_INFO("Begin to format disk:%s ...", tray_name);
	size_t dev_cap = ioengine->get_device_cap();
	if (dev_cap < (10LL << 30)) {
		S5LOG_WARN("Seems you are using a very small device with only %dGB capacity", dev_cap >> 30);
	}
	ret = clean_meta_area(ioengine, app_context.meta_size);
	if (ret) {
		S5LOG_ERROR("Failed to clean meta area with zero, disk:%s, rc:%d", tray_name, ret);
		return ret;

	}
	if ((ret = initialize_store_head()) != 0)
	{
		S5LOG_ERROR("initialize_store_head failed rc:%d", ret);
		return ret;
	}
	int obj_count = (int)((head.tray_capacity - head.meta_size) >> head.objsize_order);

	ret = free_obj_queue.init(obj_count);
	if (ret)
	{
		S5LOG_ERROR("free_obj_queue initialize failed ret(%d)", ret);
		return ret;
	}
	DeferCall _1([this](){free_obj_queue.destroy();});
	for (int i = 0; i < obj_count; i++)
	{
		free_obj_queue.enqueue(i);
	}
	ret = trim_obj_queue.init(obj_count);
	if (ret)
	{
		S5LOG_ERROR("trim_obj_queue initialize failed ret(%d)", ret);
		return ret;
	}
	DeferCall _2([this]() {trim_obj_queue.destroy(); });

	obj_lmt.clear(); //ensure no entry in LMT
	redolog = new PfRedoLog();
	DeferCall _3([this]() {delete redolog; redolog=NULL; });
	ret = redolog->init(this);
	if (ret) {
		S5LOG_ERROR("reodolog initialize failed ret(%d)", ret);
		return ret;
	}
	save_meta_data(head.current_metadata);
	char uuid_str[64];
	uuid_unparse(head.uuid, uuid_str);
	S5LOG_INFO("format disk:%s complete, uuid:%s obj_count:%d obj_size:%lld.", tray_name, uuid_str, obj_count, head.objsize);
	return 0;
}
#ifdef WITH_PFS2
int PfFlashStore::shared_disk_init(const char* tray_name, uint16_t* p_id)
{
	int ret = 0;
	this->is_shared_disk = true;
	PfEventThread::init(tray_name, MAX_AIO_DEPTH * 2, *p_id);
	safe_strcpy(this->tray_name, tray_name, sizeof(this->tray_name));
	S5LOG_INFO("Loading shared disk %s ...", tray_name);
	Cleaner err_clean;
	fd = open(tray_name, O_RDWR | O_DIRECT);
	if (fd == -1) {
		return -errno;
	}
	DeferCall _1([this]() {::close(fd); });

	/*init ioengine first*/
	//TODO: change io engine according to config file
	ioengine = new PfAioEngine(tray_name, this->fd, g_app_ctx);
	//ioengine = new PfIouringEngine(this);
	ioengine->init();
	DeferCall _2([this]() { delete ioengine; ioengine=NULL; });

	if ((ret = read_store_head()) == 0)
	{
		//not load metadata until become the owner
	}
	else if (ret == -EUCLEAN)
	{
		S5LOG_WARN("New disk found, initializing  (%s) now ...", tray_name);
		if (!is_disk_clean(ioengine))
		{
			S5LOG_ERROR("disk %s is not clean and will not be initialized.", tray_name);
			return ret;
		}
		for(int i=0;i<10;i++) {
			if (pf_ats_lock(fd, OFFSET_GLOBAL_META_LOCK) == 0) {
				DeferCall _1([this](){ pf_ats_unlock(fd, OFFSET_GLOBAL_META_LOCK);});
				if (read_store_head() == 0) {
					S5LOG_INFO("Disk:%s has been initialized by other node, UUID:%s", tray_name, uuid_str);
					return 0;
				}
				else {
					ret = format_disk();
					if(ret) {
						S5LOG_ERROR("Failed to format disk:%s, rc:%d", tray_name, ret);
						return ret;
					}
				}
			}
			sleep(1);//wait partner to release lock
		}
		S5LOG_ERROR("Failed to get ATS lock on disk:%s", tray_name);
		return -EBUSY;
	}
	else
		return ret;

	in_obj_offset_mask = head.objsize - 1;

	return ret;
}

int PfFlashStore::owner_init()
{
	int ret = 0;
	S5LOG_INFO("Become owner of disk %s ...", tray_name);
	Cleaner err_clean;
	fd = open(tray_name, O_RDWR | O_DIRECT);
	if (fd == -1) {
		return -errno;
	}
	err_clean.push_back([this]() {::close(fd); });

	/*init ioengine first*/
	//TODO: change io engine according to config file
	ioengine = new PfAioEngine(this->tray_name, fd, g_app_ctx);
	//ioengine = new PfIouringEngine(this);
	ioengine->init();

	if ((ret = read_store_head()) == 0)
	{
		ret = start_metadata_service(false);
	}
	else {
		S5LOG_ERROR("Failed to load head from disk:%s rc:%d", tray_name, ret);
		return ret;
	}
	in_obj_offset_mask = head.objsize - 1;

	trimming_thread = std::thread(&PfFlashStore::trim_proc, this);

	err_clean.cancel_all();
	return ret;
}
#endif //WITH_PFS2

char const_zero_page[4096] = { 0 };

uint64_t PfFlashStore::get_meta_position(int meta_type, int which)
{
	switch (meta_type) {
		case FREELIST:
			if (which == CURRENT)
				return head.current_metadata == FIRST_METADATA_ZONE ?
					head.free_list_position_first : head.free_list_position_second;
			else
				return head.current_metadata == FIRST_METADATA_ZONE ?
					head.free_list_position_second : head.free_list_position_first;
		case TRIMLIST:
			if (which == CURRENT)
				return head.current_metadata == FIRST_METADATA_ZONE ?
					head.trim_list_position_first : head.trim_list_position_second;
			else
				return head.current_metadata == FIRST_METADATA_ZONE ?
					head.trim_list_position_second : head.trim_list_position_first;
		case LMT:
			if (which == CURRENT)
				return head.current_metadata == FIRST_METADATA_ZONE ?
					head.lmt_position_first : head.lmt_position_second;
			else
				return head.current_metadata == FIRST_METADATA_ZONE ?
					head.lmt_position_second : head.lmt_position_first;
		case REDOLOG:
			if (which == CURRENT)
				return head.current_redolog == FIRST_REDOLOG_ZONE ?
					head.redolog_position_first : head.redolog_position_second;
			else
				return head.current_redolog == FIRST_REDOLOG_ZONE ?
					head.redolog_position_second : head.redolog_position_first;
		default:
			S5LOG_FATAL("Unknown type:%d", meta_type);
	}
	return 0; //never reach here
}

const char* PfFlashStore::meta_positon_2str(int meta_type, int which)
{
	switch (meta_type) {
		case REDOLOG:
			if (which == FIRST_REDOLOG_ZONE)
				return "[FIRST_LOGZONE]" ;
			else
				return "[SECOND_LOGZONE]" ;
		case METADATA:
			if (which == FIRST_METADATA_ZONE)
				return "[FIRST_METAZONE]" ;
			else
				return "[SECOND_METAZONE]" ;
		default:
			S5LOG_FATAL("Unknown type:%d", meta_type);
	}
	return NULL;//never here
}


int PfFlashStore::oppsite_md_zone()
{
	return (head.current_metadata == FIRST_METADATA_ZONE) ? SECOND_METADATA_ZONE : FIRST_METADATA_ZONE;
}

int PfFlashStore::oppsite_redolog_zone()
{
	return (head.current_redolog == FIRST_REDOLOG_ZONE) ? 
		SECOND_REDOLOG_ZONE : FIRST_REDOLOG_ZONE;
}

int PfFlashStore::start_metadata_service(bool init)
{
	int ret;

	if (!init) {
		ret = load_meta_data(head.current_metadata, false);
		if (ret)
			return ret;
		redolog = new PfRedoLog();
		ret = redolog->init(this);
		if (ret)
		{
			S5LOG_ERROR("reodolog initialize failed rc:%d", ret);
			return ret;
		}
		ret = redolog->replay(head.redolog_phase, CURRENT);
		if (ret)
		{
			S5LOG_ERROR("Failed to replay CURRENT redo log, rc:%d", ret);
			return ret;
		}
		S5LOG_INFO("After first replay, key:%d total obj count:%d free obj count:%d, in triming:%d, obj size:%lld", obj_lmt.size(),
			free_obj_queue.queue_depth - 1, free_obj_queue.count(), trim_obj_queue.count(), head.objsize);

		ret = redolog->replay(++head.redolog_phase, OPPOSITE);
		if (ret)
		{
			S5LOG_ERROR("Failed to replay OPPOSITE redo log, rc:%d", ret);
			return ret;
		}
		S5LOG_INFO("After second replay, key:%d total obj count:%d free obj count:%d, in triming:%d, obj size:%lld", obj_lmt.size(),
			free_obj_queue.queue_depth - 1, free_obj_queue.count(), trim_obj_queue.count(), head.objsize);

		post_load_fix();
		post_load_check();
		save_meta_data(oppsite_md_zone());
		redolog->set_log_phase(head.redolog_phase, get_meta_position(REDOLOG, CURRENT));
		S5LOG_INFO("after save metadata, key:%d total obj count:%d free obj count:%d, in triming:%d, obj size:%lld", obj_lmt.size(),
			free_obj_queue.queue_depth - 1, free_obj_queue.count(), trim_obj_queue.count(), head.objsize);
		S5LOG_INFO("load metadata finished, current metadata zone:%s, redo log will record at %s with phase:%ld",
		meta_positon_2str(METADATA, head.current_metadata), meta_positon_2str(REDOLOG, head.current_redolog), head.redolog_phase);
	} else {
		S5LOG_WARN("New disk found, initializing  (%s) now ...", tray_name);
		if (!is_disk_clean(ioengine))
		{
			S5LOG_ERROR("disk %s is not clean and will not be initialized.", tray_name);
			return -EUCLEAN;
		}
		size_t dev_cap = ioengine->get_device_cap();
		if (dev_cap < (10LL << 30)) {
			S5LOG_WARN("Seems you are using a very small device with only %dGB capacity", dev_cap >> 30);
		}
		ret = clean_meta_area(ioengine, app_context.meta_size);
		if (ret) {
			S5LOG_ERROR("Failed to clean meta area with zero, disk:%s, rc:%d", tray_name, ret);
			return ret;

		}
		if ((ret = initialize_store_head()) != 0)
		{
			S5LOG_ERROR("initialize_store_head failed rc:%d", ret);
			return ret;
		}
		int obj_count = (int)((head.tray_capacity - head.meta_size) >> head.objsize_order);

		ret = free_obj_queue.init(obj_count);
		if (ret)
		{
			S5LOG_ERROR("free_obj_queue initialize failed ret(%d)", ret);
			return ret;
		}
		for (int i = 0; i < obj_count; i++)
		{
			free_obj_queue.enqueue(i);
		}
		ret = trim_obj_queue.init(obj_count);
		if (ret)
		{
			S5LOG_ERROR("trim_obj_queue initialize failed ret(%d)", ret);
			return ret;
		}
		ret = lmt_entry_pool.init(free_obj_queue.queue_depth * 2);
		if (ret) {
			S5LOG_ERROR("Failed to init lmt_entry_pool, disk:%s rc:%d", tray_name, ret);
			return ret;
		}

		obj_lmt.reserve(obj_count * 2);
		redolog = new PfRedoLog();
		ret = redolog->init(this);
		if (ret) {
			S5LOG_ERROR("reodolog initialize failed ret(%d)", ret);
			return ret;
		}
		save_meta_data(head.current_metadata);
		redolog->set_log_phase(head.redolog_phase, get_meta_position(REDOLOG, CURRENT));
		S5LOG_INFO("Init new disk (%s) complete, obj_count:%d obj_size:%lld.", tray_name, obj_count, head.objsize);
		S5LOG_INFO("current metadata zone:%s, redo log will record at %s with phase:%ld",
			meta_positon_2str(METADATA, head.current_metadata),meta_positon_2str(REDOLOG, head.current_redolog), head.redolog_phase);
	}

	compact_tool_init();
	redolog->start();

	return 0;
}

/**
 * init flash store from tray. this function will create meta data
 * and initialize the tray if a tray is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  tray not exist or failed to open
 */

int PfFlashStore::init(const char* tray_name, uint16_t *p_id)
{
	int ret = 0;
	this->is_shared_disk = false;
	PfEventThread::init(tray_name, MAX_AIO_DEPTH * 2, *p_id);
	safe_strcpy(this->tray_name, tray_name, sizeof(this->tray_name));
	S5LOG_INFO("Loading disk %s ...", tray_name);
	Cleaner err_clean;
	fd = open(tray_name, O_RDWR | O_DIRECT);
	if (fd == -1) {
		return -errno;
	}
	err_clean.push_back([this]() {::close(fd); });


	/*init ioengine first*/
	//TODO: change io engine according to config file
	ioengine = new PfAioEngine(tray_name, fd, g_app_ctx);
	//ioengine = new PfIouringEngine(this);
	ioengine->init();

	if ((ret = read_store_head()) == 0)
	{
		ret = start_metadata_service(false);
	}
	else if (ret == -EUCLEAN)
	{
		ret = start_metadata_service(true);
	}
	else
		return ret;

	in_obj_offset_mask = head.objsize - 1;
	trimming_thread = std::thread(&PfFlashStore::trim_proc, this);

	pthread_mutex_init(&md_lock, NULL);
	pthread_cond_init(&md_cond, NULL);
	to_run_compact.store(COMPACT_IDLE);
	compact_lmt_exist = 0;

	err_clean.cancel_all();
	return ret;
}

/**
 * read data to buffer.
 * a LBA is a block of data 4096 bytes.
 * @return actual data length that readed to buf, in lba.
 *         negative value for error
 * actual data length may less than nlba in request to read. in this case, caller
 * should treat the remaining part of buffer as 0.
 */
inline int PfFlashStore::do_read(IoSubTask* io)
{
	PfMessageHead* cmd = io->parent_iocb->cmd_bd->cmd_bd;

	lmt_key key = {VOLUME_ID(io->rep_id), (int64_t)vol_offset_to_block_slba(cmd->offset, head.objsize_order), 0, 0 };
	auto block_pos = obj_lmt.find(key);
	lmt_entry *entry = NULL;
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
int PfFlashStore::do_write(IoSubTask* io)
{
	PfMessageHead* cmd = io->parent_iocb->cmd_bd->cmd_bd;
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	io->opcode = cmd->opcode;
	lmt_key key = {VOLUME_ID(io->rep_id), (int64_t)vol_offset_to_block_slba(cmd->offset, head.objsize_order), 0, 0};
	auto block_pos = obj_lmt.find(key);
	lmt_entry *entry = NULL;

	if (unlikely(block_pos == obj_lmt.end())) {
		//S5LOG_DEBUG("Alloc object for rep:0x%llx slba:0x%llx  cmd offset:0x%llx ", io->rep_id, key.slba, cmd->offset);
		if (free_obj_queue.is_empty())	{
			S5LOG_ERROR("Disk:%s is full!", tray_name);
			app_context.error_handler->submit_error(io, MSG_STATUS_NOSPACE);
			return 0;
		}
		int obj_id = free_obj_queue.dequeue();//has checked empty before and will never fail
		entry = lmt_entry_pool.alloc();
		*entry = lmt_entry { offset: obj_id_to_offset(obj_id),
			snap_seq : cmd->snap_seq,
			status : EntryStatus::NORMAL,
			prev_snap : NULL,
			waiting_io : NULL
		};
		obj_lmt[key] = entry;
		int rc = redolog->log_allocation(&key, entry, free_obj_queue.head);
		if (rc) {
			app_context.error_handler->submit_error(io, MSG_STATUS_LOGFAILED);
			S5LOG_ERROR("log_allocation error, rc:%d", rc);
			return 0;
		}
	} else {
		//static int dirty_bit=0;
		entry = block_pos->second;
		if(unlikely(entry->status == EntryStatus::RECOVERYING)) {
			if(0 == entry->snap_seq) {
				entry->snap_seq = cmd->snap_seq;
			}

			if(cmd->snap_seq != entry->snap_seq){ //
				//TODO: means new snapshot created during recovery, to make recovery operation fail, 
				assert(0);
			}
			memcpy((char*)entry->recovery_buf + offset_in_block(cmd->offset, in_obj_offset_mask),
				data_bd->buf,  cmd->length);
			for(int i=0;i<cmd->length/SECTOR_SIZE;i++) {
				entry->recovery_bmp->set_bit(int(offset_in_block(cmd->offset, in_obj_offset_mask)/SECTOR_SIZE) + i);
				//dirty_bit++;
				//S5LOG_DEBUG("dirty_bit cnt:%d", dirty_bit);
			}
			io->ops->complete(io, PfMessageStatus::MSG_STATUS_SUCCESS);
			return 0;
		}

		if(likely(cmd->snap_seq == entry->snap_seq)) {
			if (unlikely(entry->status != EntryStatus::NORMAL))
			{
				if(entry->status == EntryStatus::COPYING) {
					io->next = entry->waiting_io;
					entry->waiting_io = io; //insert io to waiting list
					return 0;
				}
				S5LOG_FATAL("Block in abnormal status:%d", entry->status);
				io->ops->complete(io, MSG_STATUS_INTERNAL);
				return -EINVAL;
			}

		} else if (unlikely(cmd->snap_seq < entry->snap_seq)) {
			S5LOG_ERROR("Write on snapshot not allowed! vol_id:0x%x request snap:%d, target snap:%d",
				cmd->vol_id, cmd->snap_seq , entry->snap_seq);
			io->ops->complete(io, MSG_STATUS_READONLY);
			return 0;
		} else if (unlikely(cmd->snap_seq > entry->snap_seq)) {
			if (free_obj_queue.is_empty()) {
				app_context.error_handler->submit_error(io, MSG_STATUS_NOSPACE);
				return -ENOSPC;
			}
			int obj_id = free_obj_queue.dequeue();
			struct lmt_entry* cow_entry = lmt_entry_pool.alloc();
			*cow_entry = lmt_entry { offset: obj_id_to_offset(obj_id),
					snap_seq : cmd->snap_seq,
					status : EntryStatus::COPYING,
					prev_snap : entry,
					waiting_io : NULL
			};
			obj_lmt[key] = cow_entry;
			int rc = redolog->log_allocation(&key, cow_entry, free_obj_queue.head);
			if (rc) {
				app_context.error_handler->submit_error(io, MSG_STATUS_LOGFAILED);
				S5LOG_ERROR("log_allocation error, rc:%d", rc);
				return -EIO;
			}
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
#ifdef WITH_SPDK_TRACE
	io->submit_time = spdk_get_ticks();
#endif
	ioengine->submit_io(io, entry->offset + offset_in_block(cmd->offset, in_obj_offset_mask), cmd->length);
	return 0;
}

int PfFlashStore::initialize_store_head()
{
	memset(&head, 0, sizeof(head));
	head.magic = 0x3553424e; //magic number, NBS5
	head.version= S5_VERSION; //S5 version
	uuid_generate(head.uuid);
	uuid_unparse(head.uuid, uuid_str);
	S5LOG_INFO("generate disk uuid:%s", uuid_str);
	head.key_size=sizeof(lmt_key);
	head.entry_size=sizeof(lmt_entry);
	head.objsize=DEFAULT_OBJ_SIZE;
	head.objsize_order=DEFAULT_OBJ_SIZE_ORDER; //objsize = 2 ^ objsize_order
	head.tray_capacity = ioengine->get_device_cap();
	head.meta_size = app_context.meta_size;
	head.free_list_position_first = OFFSET_FREE_LIST_FIRST;
	head.free_list_size_first = (128LL << 20) - 4096;
	head.free_list_position_second = OFFSET_FREE_LIST_SECOND;
	head.free_list_size_second = (128LL << 20);
	head.trim_list_position_first = OFFSET_TRIM_LIST_FIRST;
	head.trim_list_position_second = OFFSET_TRIM_LIST_SECOND;
	head.trim_list_size = (128LL << 20);
	head.lmt_position_first = OFFSET_LMT_MAP_FIRST;
	head.lmt_position_second = OFFSET_LMT_MAP_SECOND;
	head.lmt_size = (2LL << 30);
	head.redolog_position_first = OFFSET_REDO_LOG_FIRST;
	head.redolog_position_second = OFFSET_REDO_LOG_SECOND;
	head.redolog_size = REDO_LOG_SIZE;
	head.current_metadata = FIRST_METADATA_ZONE;
	head.current_redolog = FIRST_REDOLOG_ZONE;
	head.redolog_phase = 0;
	time_t time_now = time(0);
	strftime(head.create_time, sizeof(head.create_time), "%Y%m%d %H:%M:%S", localtime(&time_now));


	void *buf = align_malloc_spdk(LBA_LENGTH, LBA_LENGTH, NULL);
	if(!buf)
	{
		S5LOG_ERROR("Failed to alloc memory");
		return -ENOMEM;
	}
	memset(buf, 0, LBA_LENGTH);
	memcpy(buf, &head, sizeof(head));
	int rc = 0;
	if (LBA_LENGTH != ioengine->sync_write(buf, LBA_LENGTH, 0))
	{
		rc = -errno;
		goto release1;
	}
release1:
	free_spdk(buf);
	return rc;
}


class LmtEntrySerializer {
public:
	size_t offset;
	void* buf;
	size_t buf_size;
	off_t pos;
	MD5Stream* md5_stream;
	LmtEntrySerializer(off_t offset, void* ser_buf, unsigned int ser_buf_size, bool write, MD5Stream* stream);
	~LmtEntrySerializer();
	int read_key(lmt_key* key);
	int write_key(lmt_key* key);

	int read_entry(lmt_entry* entry);
	int write_entry(lmt_entry* entry);

	int load_buffer();
	int flush_buffer();
};
LmtEntrySerializer::LmtEntrySerializer(off_t offset, void* ser_buf, unsigned int ser_buf_size, bool write, MD5Stream* stream)
{
	this->offset = offset;
	buf = ser_buf;
	buf_size = ser_buf_size;
	memset(buf, 0, ser_buf_size);
	pos = write ? 0 : buf_size; //set pos to end of buffer, so a read will be triggered on first read
	md5_stream = stream;
}
LmtEntrySerializer::~LmtEntrySerializer()
{
	//buf is external provided, I'm not owner and should not free it
	//free(buf);
}

int LmtEntrySerializer::read_key(lmt_key* key)
{
	if (pos + sizeof(lmt_key) > buf_size)
	{
		int rc = load_buffer();
		if (rc)
			return rc;
	}
	*key = *((lmt_key*)((char*)buf + pos));
	pos += sizeof(lmt_key);
	return 0;
}
int LmtEntrySerializer::read_entry(lmt_entry* entry)
{
	if (pos + sizeof(lmt_entry) > buf_size)
	{
		int rc = load_buffer();
		if (rc)
			return rc;
	}
	*entry = *((lmt_entry*)((char*)buf + pos));
	pos += sizeof(lmt_entry);
	return 0;
}
int LmtEntrySerializer::write_key(lmt_key* key)
{
	if (pos + sizeof(lmt_key) > buf_size)
	{
		int rc = flush_buffer();
		if (rc)
			return rc;
	}
	*((lmt_key*)((char*)buf + pos)) = *key;
	pos += sizeof(lmt_key);
	return 0;
}
int LmtEntrySerializer::write_entry(lmt_entry* entry)
{
	if (pos + sizeof(lmt_entry) > buf_size)
	{
		int rc = flush_buffer();
		if (rc)
			return rc;
	}
	*((lmt_entry*)((char*)buf + pos)) = *entry;
	pos += sizeof(lmt_entry);
	return 0;
}
int LmtEntrySerializer::load_buffer()
{
	int rc = md5_stream->read_calc(buf, buf_size, offset);
	if (rc == -1)
	{
		rc = -errno;
		S5LOG_ERROR("Failed to read metadata, offset:%ld, rc:%ld", offset, rc);
		return rc;
	}
	offset += buf_size;
	pos = 0;
	return 0;
}

int LmtEntrySerializer::flush_buffer()
{
	int rc = md5_stream->write_calc(buf, buf_size, offset);
	if (rc == -1)
	{
		rc = -errno;
		S5LOG_ERROR("Failed to write metadata, offset:%ld, rc:%d", offset, rc);
		return rc;
	}
	offset += buf_size;
	pos = 0;
	return 0;
}


template <typename T>
static int save_fixed_queue(PfFixedSizeQueue<T>* q, MD5Stream* stream, off_t offset, char* buf, int buf_size)
{
	memset(buf, 0, buf_size);
	int* buf_as_int = (int*)buf;
	buf_as_int[0] = q->queue_depth;
	buf_as_int[1] = q->head;
	buf_as_int[2] = q->tail;
	if (-1 == stream->write_calc(buf, LBA_LENGTH, offset))
	{
		return -errno;
	}
	size_t src = 0;
	while(src < q->queue_depth*sizeof(T))
	{
		memset(buf, 0, buf_size);
		size_t s = std::min(q->queue_depth * sizeof(T) - src, (size_t)buf_size);
		memcpy(buf, ((char*)q->data) + src, s);
		if (-1 == stream->write_calc(buf, up_align(s, LBA_LENGTH), offset + src + LBA_LENGTH))
		{
			return -errno;
		}
		src += s;
	}
	return 0;
}

template<typename T>
static int load_fixed_queue(PfFixedSizeQueue<T>* q, MD5Stream* stream, off_t offset, char* buf, int buf_size)
{
	int rc = stream->read_calc(buf, LBA_LENGTH, offset);
	if (rc != 0)
		return rc;
	int* buf_as_int = (int*)buf;
	rc = q->init(buf_as_int[0]-1);
	if (rc)
		return rc;

	q->head = buf_as_int[1];
	q->tail = buf_as_int[2];

	size_t src = 0;
	while (src < q->queue_depth * sizeof(T))
	{
		memset(buf, 0, buf_size);
		unsigned long s = std::min(q->queue_depth * sizeof(T) - src, (size_t)buf_size);
		if (0 != stream->read_calc(buf, up_align(s, LBA_LENGTH), offset + src + LBA_LENGTH))
		{
			return -errno;
		}
		memcpy(((char*)q->data) + src, buf, s);
		src += s;
	}

	return 0;
}
/*
  SSD head layout in LBA(4096 byte):
  0: length 1 LBA, head page
  LBA 1: length 1 LBA, free obj queue meta
  LBA 2: ~ 8193: length 8192 LBA: free obj queue data,
     4 byte per object item, 8192 page can contains
         8192 page x 4096 byte per page / 4 byte per item = 8 Million items
     while one object is 4M or 16M, 4 Million items means we can support disk size 32T or 128T
  offset 64MB: length 1 LBA, trim obj queue meta
  offset 64MB + 1LBA: ~ 64MB + 8192 LBA, length 8192 LBA: trim obj queue data, same size as free obj queue data
  offset 128MB: length 512MB,  lmt map, at worst case, each lmt_key and lmt_enty mapped one to one. 8 Million items need
          8M x ( 32 Byte key + 32 Byte entry) = 512M byte = 64K
  offset 1GByte - 4096, md5 of SSD meta
  offset 1G: length 1GB, duplicate of first 1G area
  offset 2G: length 512MB, redo log
  offset 2G+512M, length 32MB,  ATS lock area
	 - 1st LBA(4K)  global meta lock
	 - others, reserve as volume lock
*/
int PfFlashStore::save_meta_data(int md_zone)
{
	S5LOG_INFO("Begin to save metadata at zone:%s, %d keys in lmt",
		meta_positon_2str(METADATA, md_zone), obj_lmt.size());

	int buf_size = 1 << 20;
	HeadPage head_tmp;
	void *buf = align_malloc_spdk(LBA_LENGTH, buf_size, NULL);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in save_meta_data");
		return -ENOMEM;
	}
	DeferCall _c([buf, this]()->void {
		free_spdk(buf);
	});

	uint64_t fl_position = (md_zone == FIRST_METADATA_ZONE) ? head.free_list_position_first : head.free_list_position_second;
	uint64_t tl_position = (md_zone == FIRST_METADATA_ZONE) ? head.trim_list_position_first : head.trim_list_position_second;
	uint64_t lmt_position = (md_zone == FIRST_METADATA_ZONE) ? head.lmt_position_first : head.lmt_position_second;
	char *md5_result = (md_zone == FIRST_METADATA_ZONE) ? head.md5_first : head.md5_second;

	int rc = 0;
	MD5Stream_ISA_L stream(fd);
	if (app_context.engine == SPDK)
		stream.spdk_eng_init(ioengine);
	rc = stream.init();
	if (rc) return rc;
	S5LOG_DEBUG("Save free_obj_queue to pos:%ld", fl_position);
	rc = save_fixed_queue<int32_t>(&free_obj_queue, &stream, fl_position, (char*)buf, buf_size);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed to save free obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = save_fixed_queue<int32_t>(&trim_obj_queue, &stream, tl_position, (char*)buf, buf_size);
	if (rc != 0)
	{
		S5LOG_ERROR("Failed to save trim obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}

	memset(buf, 0, buf_size);
	int* buf_as_int = (int*)buf;
	buf_as_int[0] = (int)obj_lmt.size();
	buf_as_int[1] = (int)sizeof(struct lmt_entry);
	buf_as_int[2] = 0;
	if (-1 == stream.write_calc(buf, LBA_LENGTH, lmt_position))
	{
		rc = -errno;
		S5LOG_ERROR("Failed to save lmt head, disk:%s rc:%d", tray_name, rc);
		return rc;
	}

	LmtEntrySerializer ser(lmt_position + LBA_LENGTH, buf, buf_size, true, &stream);
	for (auto it : obj_lmt)
	{
		lmt_key k = it.first;
		lmt_entry *v = it.second;
		rc = ser.write_key(&k);
		while (rc == 0 && v)
		{
			rc = ser.write_entry(v);
			v = v->prev_snap;
		}
		if (rc) {
			S5LOG_ERROR(" Failed to store block map.");
			return rc;
		}
	}
	rc = ser.flush_buffer();
	stream.finalize(md5_result, 0);
	// store info for rollback
	head_tmp.current_metadata = head.current_metadata;
	head_tmp.redolog_phase = head.redolog_phase;
	head_tmp.current_redolog = head.current_redolog;
	memset(buf, 0, LBA_LENGTH);
	head.current_metadata = (uint8_t)md_zone;
	redolog->discard();
	memcpy(buf, &head, sizeof(head));
	/*
	 * metadata/redolog zone & redolog phase & md5 will persist at end
	 */
	if (LBA_LENGTH != ioengine->sync_write(buf, LBA_LENGTH, 0))
	{
		S5LOG_ERROR("failed to persist head!");
		// rollback memory info if persist head error
		head.current_metadata = head_tmp.current_metadata;
		head.redolog_phase = head_tmp.redolog_phase;
		head.current_redolog = head_tmp.current_redolog;
		return -errno;		
	}
	S5LOG_INFO("Successed save metadata");
	return 0;
}

int PfFlashStore::load_meta_data(int md_zone, bool compaction)
{
	S5LOG_INFO("load metadata from md zone:%s, compaction:%d",
		meta_positon_2str(METADATA, md_zone), compaction);
	int buf_size = 1 << 20;
	void *buf = align_malloc_spdk(LBA_LENGTH, buf_size, NULL);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in load_meta_data");
		return -ENOMEM;
	}
	DeferCall _c([buf, this]() {
		free_spdk(buf);
	});

	uint64_t fl_position = (md_zone == FIRST_METADATA_ZONE) ? head.free_list_position_first : head.free_list_position_second;
	uint64_t tl_position = (md_zone == FIRST_METADATA_ZONE) ? head.trim_list_position_first : head.trim_list_position_second;
	uint64_t lmt_position = (md_zone == FIRST_METADATA_ZONE) ? head.lmt_position_first : head.lmt_position_second;
	char *md5_result = (md_zone == FIRST_METADATA_ZONE) ? head.md5_first : head.md5_second;
	
	int rc = 0;
	MD5Stream_ISA_L stream(fd);
	if (app_context.engine == SPDK)
		stream.spdk_eng_init(ioengine);
	rc = stream.init();
	if (rc)
	{
		S5LOG_ERROR("Failed to init md5 stream, disk:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = load_fixed_queue<int32_t>(&free_obj_queue, &stream, fl_position, (char*)buf, buf_size);
	if(rc)
	{
		S5LOG_ERROR("Failed to load free obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = load_fixed_queue<int32_t>(&trim_obj_queue, &stream, tl_position, (char*)buf, buf_size);
	if (rc)
	{
		S5LOG_ERROR("Failed to load trim obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}

	if (!compaction) {
		rc = lmt_entry_pool.init(free_obj_queue.queue_depth * 2);
		if (rc)
		{
			S5LOG_ERROR("Failed to init lmt_entry_pool, disk:%s rc:%d", tray_name, rc);
			return rc;
		}
	}

	uint64_t obj_count = (head.tray_capacity - head.meta_size) >> head.objsize_order;
	obj_lmt.reserve(obj_count * 2);
	if (stream.read_calc(buf, LBA_LENGTH, lmt_position) == -1)
	{
		rc = -errno;
		S5LOG_ERROR("read block entry head failed rc:%d", rc);
	}
	int *buf_as_int = (int*)buf;

	int key_count = buf_as_int[0];

	LmtEntrySerializer reader(lmt_position + LBA_LENGTH, buf, buf_size, 0, &stream);
	for (int i = 0; i < key_count; i++)
	{
		lmt_key k;
		lmt_entry *head_entry = lmt_entry_pool.alloc();
		head_entry->prev_snap = NULL;
		head_entry->waiting_io = NULL;
		rc = reader.read_key(&k);
		if (rc) {
			S5LOG_ERROR("Failed to read key.");
			return rc;
		}
		rc = reader.read_entry(head_entry);
		if (rc) {
			S5LOG_ERROR("Failed to read entry.");
			return rc;
		}
		lmt_entry *tail = head_entry;
		while (tail->prev_snap != NULL)
		{
			lmt_entry * b = lmt_entry_pool.alloc();
			b->prev_snap = NULL;
			b->waiting_io = NULL;
			rc = reader.read_entry(b);
			if (rc) {
				S5LOG_ERROR("Failed to read entry.");
				return rc;
			}
			tail->prev_snap = b;
			S5LOG_DEBUG("read entry key:{rep_id:0x%x, slba:%d}, snap:%d phy_off:0x%llx", k.vol_id, k.slba, b->snap_seq, b->offset);
			tail = tail->prev_snap;
		}

		delete_matched_entry(&head_entry,
			[](struct lmt_entry* _entry)->bool {
				return _entry->status == EntryStatus::RECOVERYING;
			},
			[this](struct lmt_entry* _entry)->void {
				lmt_entry_pool.free(_entry);
			});
		if (head_entry)
			obj_lmt[k] = head_entry;
	}
	/*just for update md5*/
	if (key_count == 0)
		reader.load_buffer();

	rc = stream.finalize(md5_result, true);
	if (rc) {
		S5LOG_ERROR("metadata md5 error!");
	}
	S5LOG_INFO("Load block map, key:%d total obj count:%d free obj count:%d", key_count,
		 free_obj_queue.queue_depth -1, free_obj_queue.count());

	return 0;

}

int PfFlashStore::compact_tool_init()
{
	int rc = 0;
	// compact tool without thread in thread pool
	compact_tool = new PfFlashStore(0);
	if (!compact_tool) {
		S5LOG_ERROR("failed to new compact_tool");
		return -ENOMEM;
	}

	S5LOG_INFO("begin to init compact tool!");
	compact_tool->redolog = new PfRedoLog();
	if (!compact_tool->redolog) {
		rc = -ENOMEM;
		S5LOG_ERROR("failed to new redolog!");
		goto err0;
	}
	compact_tool->redolog->init(compact_tool);

	rc = compact_tool->lmt_entry_pool.init(free_obj_queue.queue_depth * 2);
	if (rc) {
		S5LOG_ERROR("Failed to init lmt_entry_pool for compact tool, disk:%s rc:%d", tray_name, rc);
		goto err1;
	}
	compact_tool->ioengine = ioengine;
	compact_tool->ns = ns;
	compact_tool->head = head;
	memcpy(compact_tool->tray_name, tray_name, sizeof(tray_name));
	memcpy(compact_tool->uuid_str, uuid_str, sizeof(uuid_str));

	S5LOG_INFO("init compact tool success!");
	return 0;
err1:
	delete compact_tool->redolog;
err0:
	delete compact_tool;
	return rc;
}

/*
 *   not run on ssd thread
 *   take notice:
 *	   some struct of PfFlashStore will used outside ssd_thread, 
 *     we should make sure that they are thread safe. 
 */
int PfFlashStore::compact_meta_data()
{
	int rc;

	S5LOG_INFO("begin to compact metadata for %s", tray_name);
	S5LOG_DEBUG("before compact_meta_data, disk:%s, key:%d total obj count:%d free obj count:%d, in triming:%d, obj size:%lld",
		tray_name, obj_lmt.size(),
		free_obj_queue.queue_depth - 1, free_obj_queue.count(), trim_obj_queue.count(), head.objsize);

	compact_tool->head = head;

	/*load metadata at current md zone if compact lmt is empty*/
	if (compact_lmt_exist == 0) {
		rc = compact_tool->load_meta_data(head.current_metadata, true);
		if (rc) {
			S5LOG_ERROR("failed to load metadata while compaction! %s", tray_name);
			goto out1;
		}
	}

	rc = compact_tool->redolog->replay(head.redolog_phase, CURRENT);
	if (rc) {
		S5LOG_ERROR("Failed to replay rlog, disk:%s rc:%d", tray_name, rc);
		goto out1;
	}

	/*save meta to opposite zone*/
	rc = compact_tool->save_meta_data(oppsite_md_zone());
	if (rc) {
		S5LOG_ERROR("failed to save metadata while compaction! %s", tray_name);
		goto out1;
	}
	// metadata/redolog zone & redolog phase & md5 already changed at save_meta_data
	head = compact_tool->head;
	compact_lmt_exist = 1;

	S5LOG_INFO("compact metadata for %s done, rc:%d", tray_name, rc);
	S5LOG_DEBUG("after compact_meta_data, disk:%s, key:%d total obj count:%d free obj count:%d, in triming:%d, obj size:%lld",
		tray_name, obj_lmt.size(),
		free_obj_queue.queue_depth - 1, free_obj_queue.count(), trim_obj_queue.count(), head.objsize);
	return 0;

out1:
	compact_tool->free_obj_queue.destroy();
	compact_tool->trim_obj_queue.destroy();
	for(auto it = compact_tool->obj_lmt.begin();it != compact_tool->obj_lmt.end();++it) {
		lmt_entry *head = it->second;
		while (head) {
			lmt_entry *p = head;
			head = head->prev_snap;
			compact_tool->lmt_entry_pool.free(p);
		}
	}
	compact_tool->obj_lmt.clear();
	compact_lmt_exist = 0;

	return rc;
}


/*
*	state: COMPACT_IDLE, COMPACT_TODO, COMPACT_STOP;
*   COMPACT_IDLE: metadata compaction thread will run every 1600s if state is COMPACT_IDLE;
*   COMPACT_TODO: metadata compaction thread will wakeup immediately if state is set to COMPACT_TODO;
*   COMPACT_STOP: metadata compaction thread will nerver work if state is COMPACT_STOP,
*			       it will used by we want to save metadata at ssd thread.
*   COMPACT_RUNNING： metadata compaction thread is running
*   COMPACT_ERROR： last compaction return error, will save metadata synchronously
*/
int PfFlashStore::meta_data_compaction_trigger(int state, bool force_wait)
{
	int rc;
	int last_state;

	if (!force_wait) {
		if (to_run_compact.load() == COMPACT_RUNNING) {
			S5LOG_INFO("md compacion is running");
			return 0;
		}
	}

	pthread_mutex_lock(&md_lock);
	last_state = to_run_compact.load();
	to_run_compact.store(state);
	if (state == COMPACT_TODO && last_state == COMPACT_ERROR) {
		// flush metadata synchronously
		head.redolog_phase = redolog->phase;
		rc = save_meta_data(oppsite_md_zone());
		if (rc) {
			// todo: evict disk
			S5LOG_FATAL("failed to save metadata synchronously for %s", tray_name);
		}
		redolog->set_log_phase(head.redolog_phase, get_meta_position(REDOLOG, OPPOSITE));
		to_run_compact.store(COMPACT_IDLE);
	}

	// if last state is COMPACT_ERROR, compatcion is unnecessary
	if (state == COMPACT_TODO && last_state != COMPACT_ERROR) {
		/*will persist at metadata compaction thread*/
		redolog->set_log_phase(head.redolog_phase + 1, get_meta_position(REDOLOG, OPPOSITE));
		S5LOG_INFO("send signal to save md, new log will record at log zone:%s(%lld), phase:%d",
			meta_positon_2str(REDOLOG, oppsite_md_zone()), get_meta_position(REDOLOG, OPPOSITE), redolog->phase);
		pthread_cond_signal(&md_cond); // compaction must be will
	}
	pthread_mutex_unlock(&md_lock);

	return 0;
}

int PfFlashStore::read_store_head()
{
	void *buf = align_malloc_spdk(LBA_LENGTH, LBA_LENGTH, NULL);
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
	if(head.version != S5_VERSION) //S5 version
		return -EUCLEAN;
	uuid_unparse(head.uuid, uuid_str);
	S5LOG_INFO("Load disk:%s, uuid:%s",tray_name, uuid_str );
	return 0;
}

int PfFlashStore::write_store_head()
{
	void *buf = align_malloc_spdk(LBA_LENGTH, LBA_LENGTH, NULL);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in read_store_head");
		return -ENOMEM;
	}
	DeferCall _rel([buf, this]() {
		free_spdk(buf);
	});

	memset(buf, 0, LBA_LENGTH);
	memcpy(buf, &head, sizeof(head));
	int rc = 0;
	if (LBA_LENGTH != ioengine->sync_write(buf, LBA_LENGTH, 0))
	{
		rc = -errno;
		S5LOG_ERROR("failed to write store head");
	}

	return rc;
}


int PfFlashStore::process_event(int event_type, int arg_i, void* arg_p, void*)
{
	int rc=0;
	char zk_node_name[128];
	char store_id_str[16];
	snprintf(store_id_str, sizeof(store_id_str), "%d", app_context.store_id);
	snprintf(zk_node_name, sizeof(zk_node_name), "shared_disks/%s/owner_store", uuid_str);
	switch (event_type) {
#ifdef WITH_PFS2
	case EVT_WAIT_OWNER_LOCK: //this will be the first event received for shared disk
		do {
			rc = app_context.zk_client.wait_lock(zk_node_name, store_id_str); //we will not process any event before get lock
			pthread_testcancel();
			if (rc) {
				if(exiting)
					return rc;
				S5LOG_ERROR("Unexcepted rc on contention owner, rc:%d", rc);
				sleep(1);
			} else {
				rc = owner_init(); //become to owner
				if (rc) {
					S5LOG_ERROR("Failed call owner_init");
				}
			}

		} while(rc);

		break;
#endif
	case EVT_IO_REQ:
	{
	    PfOpCode op = ((IoSubTask*)arg_p)->parent_iocb->cmd_bd->cmd_bd->opcode;
        switch(op){
            case PfOpCode::S5_OP_READ:
            case PfOpCode::S5_OP_RECOVERY_READ:
                do_read((IoSubTask*)arg_p);
                break;
            case PfOpCode::S5_OP_WRITE:
            case PfOpCode::S5_OP_REPLICATE_WRITE:
                do_write((IoSubTask*)arg_p);
                break;
            default:
                S5LOG_FATAL("Unsupported op code:%d", op);
        }

	}
	break;
	case EVT_COW_READ:
	{
		struct CowTask *req = (struct CowTask*)arg_p;
		req->opcode = S5_OP_COW_READ;
		ioengine->submit_cow_io(req, req->src_offset, req->size);
	}
	break;
	case EVT_COW_WRITE:
	{
		struct CowTask*req = (struct CowTask*)arg_p;
		req->opcode = S5_OP_COW_WRITE;
		ioengine->submit_cow_io(req, req->dst_offset, req->size);
	}
	break;
	case EVT_SAVEMD:
	{
		if (redolog->current_offset == redolog->start_offset)
			return 0;
		S5LOG_INFO("get event EVT_SAVEMD, disk:%s, key:%d total obj count:%d free obj count:%d, in triming:%d, obj size:%lld",
			tray_name, obj_lmt.size(),
			free_obj_queue.queue_depth - 1, free_obj_queue.count(), trim_obj_queue.count(), head.objsize);

		meta_data_compaction_trigger(COMPACT_TODO, false);
	}
	break;
	default:
		S5LOG_FATAL("Unimplemented event type:%d", event_type);
	}
    return 0;
}

PfFlashStore::~PfFlashStore()
{
	S5LOG_DEBUG("PfFlashStore destrutor");
}

void PfFlashStore::begin_cow(lmt_key* key, lmt_entry *srcEntry, lmt_entry *dstEntry)
{
	auto f = cow_thread_pool.commit([this, key, srcEntry, dstEntry]{do_cow_entry(key, srcEntry, dstEntry);});
}


void PfFlashStore::do_cow_entry(lmt_key* key, lmt_entry *srcEntry, lmt_entry *dstEntry)
{//this function called in thread pool, not the store's event thread
	
	int failed = 0;
	sem_t cow_sem;
	int cow_depth = 8;
	S5LOG_DEBUG("Begin COW on dev:%s from obj:%ld snap:%d to snap:%d", tray_name, offset_to_obj_id(srcEntry->offset),
			srcEntry->snap_seq, dstEntry->snap_seq);

	sem_init(&cow_sem, 0, cow_depth);
	for (int j = 0; j < head.objsize / COW_OBJ_SIZE && !failed; j++) {
		sem_wait(&cow_sem);
		std::ignore = std::async(std::launch::async, [srcEntry, dstEntry, j, this, &failed, &cow_sem]{
			void *buf = app_context.cow_buf_pool.alloc(COW_OBJ_SIZE, spdk_engine_used());
			int64_t rc = ioengine->sync_read(buf, COW_OBJ_SIZE, srcEntry->offset + j * COW_OBJ_SIZE);
			if(rc != COW_OBJ_SIZE){
				S5LOG_ERROR("Failed during cow read on dev:%s offset:0x%lx", tray_name, srcEntry->offset + j * COW_OBJ_SIZE);
				failed++;
			} else {
				rc = ioengine->sync_write(buf, COW_OBJ_SIZE, dstEntry->offset + j * COW_OBJ_SIZE);
				if (rc != COW_OBJ_SIZE) {
					S5LOG_ERROR("Failed during cow write on dev:%s offset:0x%lx", tray_name, dstEntry->offset + j * COW_OBJ_SIZE);
					failed++;
				}
			}
			app_context.cow_buf_pool.free(buf, spdk_engine_used());
			sem_post(&cow_sem);
		});
		
	}
	for (int i = 0; i < cow_depth; i++) {
		sem_wait(&cow_sem);
	}
	S5LOG_DEBUG("End COW:%s", failed ? "FAILED" : "SUCCEED");
	if(failed)
		goto cowfail;

	sync_invoke([key, srcEntry, dstEntry, this]()->int {
		EntryStatus old_status = dstEntry->status;
		dstEntry->status = EntryStatus::NORMAL;

		redolog->log_status_change(key, dstEntry, old_status);
		IoSubTask*t = dstEntry->waiting_io;
		dstEntry->waiting_io = NULL;
		while (t) {
			if (unlikely(t->opcode != S5_OP_WRITE && t->opcode != S5_OP_REPLICATE_WRITE)) {
				S5LOG_FATAL("Unexcepted op code:%d", t->opcode);
			}
			S5LOG_DEBUG("COW pending IO:%p", t);
			do_write(t);
			t = t->next;
		}
		if(srcEntry->status == DELAY_DELETE_AFTER_COW) {
			delete_obj(key, srcEntry);
		}
		return 0;
	});

	return;
cowfail:
	sync_invoke([dstEntry]()->int
	{
		IoSubTask* t = dstEntry->waiting_io;
		dstEntry->waiting_io = NULL;
		while (t) {
			S5LOG_ERROR("return REOPEN for cowfail, cid:%d", t->parent_iocb->cmd_bd->cmd_bd->command_id);
			t->ops->complete(t, PfMessageStatus::MSG_STATUS_REOPEN);
			t=t->next;
		}
		return 0;
	});

	return;
}

void PfFlashStore::begin_cow_scc(lmt_key* key, lmt_entry *objEntry, lmt_entry *dstEntry)
{
	struct scc_cow_context *sc = (struct scc_cow_context *)malloc(sizeof(struct scc_cow_context));
	sc->key = key;
	sc->srcEntry = objEntry;
	sc->dstEntry = dstEntry;
	sc->rc = 0;
	sc->pfs = this;
	S5LOG_INFO("begin scc");
	((PfspdkEngine*)ioengine)->submit_scc(DEFAULT_OBJ_SIZE, objEntry->offset, dstEntry->offset,
		end_cow_scc, sc);
}

void* PfFlashStore::end_cow_scc(void *ctx)
{
	struct scc_cow_context *sc = (struct scc_cow_context *)ctx;
	lmt_entry *dstEntry = sc->dstEntry;
	lmt_entry *srcEntry = sc->srcEntry;
	lmt_key *key = sc->key;
	IoSubTask*t = dstEntry->waiting_io;
	dstEntry->waiting_io = NULL;

	if (sc->rc == 0) {
		while (t) {
			if (unlikely(t->opcode != S5_OP_WRITE && t->opcode != S5_OP_REPLICATE_WRITE)) {
				S5LOG_FATAL("Unexcepted op code:%d", t->opcode);
			}
			sc->pfs->do_write(t);
			t = t->next;
		}

		if (srcEntry->status == DELAY_DELETE_AFTER_COW){
			sc->pfs->delete_obj(key, srcEntry);
		}
	}else{
		while (t) {
			S5LOG_ERROR("return REOPEN for cowfail, cid:%d", t->parent_iocb->cmd_bd->cmd_bd->command_id);
			t->ops->complete(t, PfMessageStatus::MSG_STATUS_REOPEN);
			t = t->next;
		}
	}

	return 0;
}

void PfFlashStore::delete_snapshot(shard_id_t shard_id, uint32_t snap_seq_to_del, uint32_t prev_snap_seq, uint32_t next_snap_seq) {
	uint64_t shard_index = SHARD_INDEX(shard_id.val());
	uint64_t vol_id = VOLUME_ID(shard_id.val());
	int obj_lba_cnt = (int)(head.objsize / LBA_LENGTH);
	for(int i=0; i<SHARD_LBA_CNT; i += obj_lba_cnt)
	{
		uint64_t start_lba = shard_index * SHARD_LBA_CNT + i;
		this->event_queue->sync_invoke([this, prev_snap_seq, next_snap_seq, snap_seq_to_del, vol_id, start_lba]()->int {
			return delete_obj_snapshot(vol_id, start_lba, snap_seq_to_del, prev_snap_seq, next_snap_seq);
		});

		
	}

}

int PfFlashStore::delete_obj_snapshot(uint64_t volume_id, int64_t slba, uint32_t snap_seq, uint32_t prev_snap_seq, uint32_t next_snap_seq)
{
	S5LOG_DEBUG("delete obj vol:0x%llx slba:%lld, snap_seq:%d prev_seq:%d next_seq:%d", volume_id, slba, snap_seq, prev_snap_seq, next_snap_seq);
	assert(pthread_self() == this->tid);//we are manipulating lmt table
	lmt_key key={.vol_id=volume_id, .slba = slba};
	auto pos = obj_lmt.find(key);
	if(pos == obj_lmt.end())
		return -ENOENT;
	/*
	 * so far, we have two lines about snap sequence
	 * 1) logically, whenever a snapshot is created, a snap sequence is created. this snap_seq list can be get from snapshot table
	 * 2) physically, depends the objected allocated in disk, since not every snapshot has object allocated in disk, this list
	 *    is a subset of logical list
	 *
	 * the snap sequence passed as argument is logical list,
	 * bellow we iterate obj list will get physical list.
	 */

	lmt_entry* target_entry = pos->second;
	lmt_entry* prev_entry = target_entry->prev_snap;
	lmt_entry* next_entry = NULL;
	while (target_entry && snap_seq < target_entry->snap_seq) {
		next_entry = target_entry;
		target_entry = target_entry->prev_snap;
		prev_entry = target_entry->prev_snap;
	}

	if( (prev_entry != NULL && prev_entry->status != EntryStatus::NORMAL)
		|| (target_entry != NULL && target_entry->status != EntryStatus::NORMAL)
		|| (next_entry != NULL && next_entry->status != EntryStatus::NORMAL)) {
		if(prev_entry != NULL && prev_entry->status != EntryStatus::NORMAL) {
			S5LOG_DEBUG("Cond1, prev status:%d", prev_entry->status);
		} else if(target_entry != NULL && target_entry->status != EntryStatus::NORMAL) {
			S5LOG_DEBUG("Cond2, target status:%d", target_entry->status);
		} else if(next_entry != NULL && next_entry->status != EntryStatus::NORMAL){
			S5LOG_DEBUG("Cond3, next status:%d", next_entry->status);
		}
		S5LOG_WARN("delete_obj_snapshot aborted, for object(vol_id:0x%llx slba:%lld)", volume_id, slba);
		return -EAGAIN;
	}

	/*
	 * next problem is to determine should we delete the target object in disk
	 * for clearify, define symbol:
	 * Lps: logical prev snap sequence
	 * Lts: logical target snap sequence, i.e. the snapshot to delete
	 * Lns: logical next snap sequence
	 *
	 * Pps: physical prev object snap_seq
	 * Pts: physical target object snap_seq
	 * Pns: physical next object snap_seq
	 *
	 * our goal is to determine how to treat physical-target object Ot, i.e. `targent_entry`, only three choice:
	 *   C1) delete this entry
	 *   C2) keep it, change its snap sequence
	 *   C3) keep it, not change its snap sequence
	 *
	 * we have the always truth:   Pts <= Lts, Lts < Pns
	 * and possible A:   1) Lps < Pps < Pts <= Lts  2) Pps <= Lps < Pts <= Lts 3) Pps < Pts < Lps < Lts
	 *              B:   1) Pts <= Lts < Pns <= Lns  2) Pts <= Lts < Lns < Pns   3) Pns not exists
	 *
	 *  A1 is illegal, Lps < Pps < Pts is impossible.
	 *
	 *
	 *  in one sentence, we should keep this object if it is needed by Lps or Lns. otherwise, delete it.
	 */
	uint32_t Lps = prev_snap_seq;
	uint32_t Lts = snap_seq;
	uint32_t Lns = next_snap_seq;

	uint32_t Pps = prev_entry == NULL ? 0 : prev_entry->snap_seq;
	uint32_t Pts = target_entry == NULL ? 0 : target_entry->snap_seq;
	uint32_t Pns = next_entry == NULL ? UINT32_MAX : next_entry->snap_seq;

	assert(Pts <= Lts);
	assert(Lts < Pns);
	if( (Lps < Pps) /*A1*/ && (Pns <= Lns) /*B1*/) {

		/*
		 *
		 * 	=case A1 B1=
		 *                Lps                  Lts                Lns
		 *                |                    |                  |
		 *                |                    |                  |
		 * Logical    ----Sp-------------------D------------------Sn------->
		 *
		 *
		 *	     	         +--+      +--+            +--+
		 * Physical   -------|Op|----- |Ot|------------|On|---------------->
		 *                   +--+      +--+            +--+
		 *                    ^         ^               ^
		 *                    |         |               |
		 *                    Pps       Pts             Pns
		 */
		S5LOG_WARN("del snapshot state A1-B1 is illegal, may be caused by previous error, and can be corrected by GC");
		if(next_entry->status == EntryStatus::NORMAL)
			delete_obj(&key, target_entry);
		else
			target_entry->status = EntryStatus::DELAY_DELETE_AFTER_COW;
		//delete_obj(&key, prev_entry);// we can also delete Pps, since it's not needed
	}
	else if( (Lps < Pps) /*A1*/ && (Lns < Pns) /*B2*/) {
		/**
		 * 	=case A1 B2=
	     *                Lps                  Lts                Lns
	     *                |                    |                  |
	     *                |                    |                  |
	     * Logical    ----Sp-------------------D------------------Sn------------------------>
	     *
	     *
	     *	     	         +--+      +--+                              +--+
	     * Physical   -------|Op|----- |Ot|------------------------------|On|---------------->
	     *                   +--+      +--+                              +--+
	     *                    ^         ^                                 ^
	     *                    |         |                                 |
	     *                    Pps       Pts                               Pns
	     */
		S5LOG_WARN("del snapshot state A1-B2 is illegal, may be caused by previous error, and can be corrected by GC");
		//keep Ot for used by Lns
		uint32_t old_snap = target_entry->snap_seq;
		target_entry->snap_seq = Lns;
		redolog->log_snap_seq_change(&key, target_entry, old_snap);
		//delete_obj(&key, prev_entry);// we can also delete Pps, since it's not needed
	}
	else if( (Lps < Pps) /*A1*/ && next_entry == NULL /*B3*/) {
		/**
		 * 	=case A1 B2=
	     *                Lps                  Lts                Lns
	     *                |                    |                  |
	     *                |                    |                  |
	     * Logical    ----Sp-------------------D------------------Sn------------------------>
	     *
	     *
	     *	     	         +--+      +--+
	     * Physical   -------|Op|----- |Ot|---------------------------------------------> ... Pns == UINT_MAX
	     *                   +--+      +--+
	     *                    ^         ^
	     *                    |         |
	     *                    Pps       Pts
	     */
	     //this condition has covered by A1-B2
	     assert(0);
	}
	else if( (Pps <= Lps) /*A2*/  && (Pns <= Lns) /*B1*/) {
		/*   A2: Pps <= Lps < Pts <= Lts,  B1: Pts <= Lts < Pns <= Lns
		 * =case A2 B1=, also same as B2, Ot is needed by Lns and can't delete
		 *
		 * 	=case A2 B1=
		 *                         Lps                  Lts                Lns
		 *                         |                    |                  |
		 *                         |                    |                  |
		 * Logical    -------------Sp-------------------D------------------Sn------->
		 *
		 *
		 *	     	       +--+          +--+                    +--+
		 * Physical   -----|Op|--  ----- |Ot|--------------------|On|------------->
		 *                 +--+          +--+                    +--+
		 *                  ^             ^                       ^
		 *                  |             |                       |
		 *                  Pps           Pts                     Pns
		 */
		S5LOG_INFO("del snapshot state A2-B1");
		if(next_entry->status == EntryStatus::NORMAL)
			delete_obj(&key, target_entry);
		else
			target_entry->status = EntryStatus::DELAY_DELETE_AFTER_COW;
	}
	else if( (Pps <= Lps) /*A2*/  && (Lns < Pns) /*B2*/) {
		/*
		 * 	=case A2 B2=
		 *                         Lps                  Lts           Lns
		 *                         |                    |             |
		 *                         |                    |             |
		 * Logical    -------------Sp-------------------D-------------Sn------->
		 *
		 *
		 *	     	         +--+        +--+                              +--+
		 * Physical   -------|Op|------- |Ot|------------------------------|On|---------------->
		 *                   +--+        +--+                              +--+
		 *                    ^           ^                                 ^
		 *                    |           |                                 |
		 *                    Pps         Pts                               Pns
		 *
		 */
		uint32_t old_snap = target_entry->snap_seq;
		target_entry->snap_seq = Lns;
		redolog->log_snap_seq_change(&key, target_entry, old_snap);
	}
	else if( (Pps <= Lps) /*A2*/  && next_entry == NULL /*B3*/) {
		/* 	=case A2 B3=
	   *                         Lps                  Lts           Lns
	   *                         |                    |             |
	   *                         |                    |             |
	   * Logical    -------------Sp-------------------D-------------Sn------->
	   *
	   *
	   *                   +--+        +--+
	   * Physical   -------|Op|------- |Ot|---------------------------------> ... Pns == UINT_MAX
	   *                   +--+        +--+
	   *                    ^           ^
	   *                    |           |
	   *                    Pps         Pts
	   */
		//this condition has covered by A2-B2
		assert(0);
	}
	else if( (Pts < Lps) /*A3*/) {

     /*
     *
     *  =case A3 B1=
     *                                      Lps                  Lts                Lns
     *                                      |                    |                  |
     *                                      |                    |                  |
     * Logical    --------------------------Sp-------------------D------------------Sn------->
     *
     *
     *	     	         +--+        +--+                             +--+
     * Physical   -------|Op|------- |Ot|-----------------------------|On|---------------->
     *                   +--+        +--+                             +--+
     *                    ^           ^                                ^
     *                    |           |                                |
     *                    Pps         Pts                              Pns
     *
     *  =case A3 B2=
     *                                      Lps        Lts       Lns
     *                                      |          |         |
     *                                      |          |         |
     * Logical    --------------------------Sp---------D---------Sn------->
     *
     *
     *	     	         +--+        +--+                             +--+
     * Physical   -------|Op|------- |Ot|-----------------------------|On|---------------->
     *                   +--+        +--+                             +--+
     *                    ^           ^                                ^
     *                    |           |                                |
     *                    Pps         Pts                              Pns
     *
     *
     *  =case A3 B3=
     *                                      Lps        Lts       Lns
     *                                      |          |         |
     *                                      |          |         |
     * Logical    --------------------------Sp---------D---------Sn------->
     *
     *
     *	     	         +--+        +--+
     * Physical   -------|Op|------- |Ot|------------------------------------------->
     *                   +--+        +--+
     *                    ^           ^
     *                    |           |
     *                    Pps         Pts
	 */
		S5LOG_INFO("del snapshot state A3-B*");
		uint32_t old_snap = target_entry->snap_seq;
		target_entry->snap_seq = Lps;
		redolog->log_snap_seq_change(&key, target_entry, old_snap);
	}
	else {
		S5LOG_FATAL("del snapshot encounter unexpected state: vol_id:0x%llx slba:%lld Lps:%d Lts:%d Lns:%d Pps:%d Pts:%d Pns:%d",
			  volume_id, slba, Lps, Lts, Lns, Pps, Pts, Pns);
	}
	return 0;
}

int PfFlashStore::delete_obj_by_snap_seq(struct lmt_key* key, uint32_t snap_seq)
{
	auto pos = obj_lmt.find(*key);
	if (pos == obj_lmt.end())
		return 0;
	delete_matched_entry(&pos->second,
		[snap_seq](struct lmt_entry* _entry)->bool {
			return _entry->snap_seq == snap_seq;
		},
		[key, this](struct lmt_entry* _entry)->void {
			trim_obj_queue.enqueue((int)offset_to_obj_id(_entry->offset));
			redolog->log_trim(key, _entry, trim_obj_queue.tail);
			lmt_entry_pool.free(_entry);
		});
	if (pos->second == NULL)
		obj_lmt.erase(pos);
	return 0;
}


int PfFlashStore::delete_obj(struct lmt_key* key, struct lmt_entry* entry)
{
	auto pos = obj_lmt.find(*key);
	if(pos == obj_lmt.end())
		return 0;
	delete_matched_entry(&pos->second,
	                     [entry](struct lmt_entry* _entry)->bool {
		                     return _entry == entry;
	                     },
	                     [key, this](struct lmt_entry* _entry)->void {
		                     trim_obj_queue.enqueue((int)offset_to_obj_id(_entry->offset));
		                     redolog->log_trim(key, _entry, trim_obj_queue.tail);
		                     lmt_entry_pool.free(_entry);
	                     });
	if(pos->second == NULL)
		obj_lmt.erase(pos);
	return 0;
}


template<typename ReplyT>
static int query_store(const char* ip, ReplyT& reply, const char* format, ...)
{
	static __thread char buffer[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	string url = format_string( "http://%s:49181/api?%s", ip, buffer);
	void* reply_buf = pf_http_get(url, 10, 3);
	if( reply_buf != NULL) {
		DeferCall _rel([reply_buf]() { free(reply_buf); });
		auto j = json::parse((char*)reply_buf);
		if(j["ret_code"].get<int>() != 0) {
			throw std::runtime_error(format_string("Failed %s, reason:%s", url.c_str(), j["reason"].get<std::string>().c_str()));
		}
		j.get_to<ReplyT>(reply);
		return 0;
	}
	return -1;
}

static void recovery_complete(SubTask* t, PfMessageStatus comp_status)
{
	t->complete_status = comp_status;
	RecoverySubTask* rt = (RecoverySubTask *)t;
	//S5LOG_INFO("RecoverySubTask:%p complete, status:%s", t, PfMessageStatus2Str(comp_status));
	rt->owner_queue->free(rt);
	sem_post(rt->sem);

	
}
static void recovery_complete_2(SubTask* t, PfMessageStatus comp_status, uint16_t meta_ver)
{
	t->ops->complete(t, comp_status);
}

TaskCompleteOps _recovery_complete_ops = { recovery_complete , recovery_complete_2 };

int PfFlashStore::recovery_write(lmt_key* key, lmt_entry * entry, uint32_t snap_seq, void* buf, size_t length, off_t offset)
{
	int rc=0;
	assert(entry != NULL);
	if(entry->snap_seq != snap_seq){
		S5LOG_FATAL("incorrect snap_seq, entry snap:%d target snap:%d", entry->snap_seq, snap_seq);
	}
	
	off_t in_blk_off = offset_in_block(offset, in_obj_offset_mask);
	rc = (int)ioengine->sync_write(buf, length, entry->offset + in_blk_off);
	if(rc == (uint64_t)length)
		return 0;
	else {
		rc = -errno;
		S5LOG_ERROR("Failed write disk:%s on recovery_write, rc:%d", tray_name, rc);
		return rc;
	}
}

int PfFlashStore::finish_recovery_object(lmt_key* key, lmt_entry * head, size_t length, off_t offset, int failed)
{
	int rc;
	assert(pthread_self () != this->tid); //this function must run in different thread than this disk's proc thread
	off_t cow_src_off=-1, cow_dst_off=-1;

	rc = this->sync_invoke([this, key, head, failed, &cow_src_off, &cow_dst_off]()->int {
		assert(head->status == EntryStatus::RECOVERYING);
		auto lmt_pos = obj_lmt.find(*key);
		if(failed || head->recovery_bmp->is_empty()) {
			lmt_pos->second = head->prev_snap;
			if(lmt_pos->second == NULL)
				obj_lmt.erase(lmt_pos);
			head->prev_snap = NULL;
//			lmt_entry_pool.free(head); //Why not free it? in `recovery_replica` will free recovery head entry
			return -EALREADY;
		}
		lmt_entry* data_entry = head->prev_snap;
		if(data_entry == NULL || head->snap_seq > data_entry->snap_seq) {
			bool need_cow = (data_entry != NULL); //this is condition 2:head->snap_seq > data_entry->snap_seq
			if (free_obj_queue.is_empty()) {
				S5LOG_ERROR("Failed to alloc object for recovery write, disk may be full. disk:%s", tray_name);
				return -ENOSPC;
			}
			int obj_id = free_obj_queue.dequeue();
			data_entry = lmt_entry_pool.alloc();
			*data_entry = lmt_entry{offset: obj_id_to_offset(obj_id),
					snap_seq : head->snap_seq,
					status : EntryStatus::COPYING,
					prev_snap : head->prev_snap,
					waiting_io : NULL
			};
			int rc = redolog->log_allocation(key, data_entry, free_obj_queue.head);
			if (rc) {
				lmt_entry_pool.free(data_entry);
				free_obj_queue.enqueue(obj_id);
				S5LOG_ERROR("Failed to log_allocation in recovery_write, rc:%d", rc);
				return rc;
			}
			head->prev_snap = data_entry;
			if(need_cow) {
				cow_src_off = data_entry->prev_snap->offset;
				cow_dst_off = data_entry->offset;
				return -ESTALE;
			}
			else
				return 0;
		}
		if(head->snap_seq < data_entry->snap_seq) {
			S5LOG_FATAL("BUG: Head snap_seq should not less than first data's");
		}

		return 0;
	});
	if(rc == -EALREADY)
		return 0;//done OK
	if(rc == -ESTALE) {
		//now begin do cow
		void *cow_buf = app_context.recovery_buf_pool.alloc(this->head.objsize, spdk_engine_used());
		DeferCall _d([cow_buf] { app_context.recovery_buf_pool.free(cow_buf, spdk_engine_used()); });

		rc = (int)ioengine->sync_read(cow_buf, this->head.objsize, cow_src_off);
		if (rc != (int)this->head.objsize) {
			rc = -errno;
			S5LOG_ERROR("Failed cow sync read on disk:%s rc:%d", tray_name, rc);
			return rc;
		}

		rc = (int)ioengine->sync_write(cow_buf, this->head.objsize, cow_dst_off);
		if (rc != (int)this->head.objsize) {
			rc = -errno;
			S5LOG_ERROR("Failed cow sync write on disk:%s rc:%d", tray_name, rc);
			return rc;
		}

	} else if(rc != 0)
		return rc;

	//overwrite pending buffer's content to object
	rc = this->sync_invoke([this, key, head, offset]()->int {
		lmt_entry* data_entry = head->prev_snap;
		//assert(offset_in_block(offset, in_obj_offset_mask) == 0); //offset is block offset
		off_t base_offset = data_entry->offset + offset_in_block(offset, in_obj_offset_mask);
		int cnt = 0;
		int min=std::numeric_limits<int>::max();
		int max=0;
		for (int i = 0; i < head->recovery_bmp->bit_count; i++) {
			if (head->recovery_bmp->is_set(i)) {
				if(i<min)min=i;
				if(i>max)max=i;
				//TODO: write with sector size(512) is very low performance, need improvement
				// if spdk support 512 write?
				//S5LOG_DEBUG("sync write at:0x%lx ", base_offset + i * SECTOR_SIZE);
				int rc = (int)ioengine->sync_write((char *) head->recovery_buf + i * SECTOR_SIZE, SECTOR_SIZE, base_offset + i * SECTOR_SIZE );
				if (rc != SECTOR_SIZE) {
					rc = -errno;
					S5LOG_ERROR("Failed write disk:%s on end_recovery, rc:%d", tray_name, rc);
					return rc;
				}
				cnt ++;
			}
		}
		S5LOG_DEBUG("%d sectors overwrited from pending buffer, on offset:0x%lx snap:%d, range index:(%d, %d)", cnt, base_offset, data_entry->snap_seq,
					min, max);
		EntryStatus old_status = data_entry->status;
		data_entry->status  = EntryStatus::NORMAL;
		redolog->log_status_change(key, data_entry, old_status);
		obj_lmt[*key] = data_entry;
		return 0;
	});
	return rc;
}

struct RecoveryContext
{
	int iodepth = 4;

	PfFlashStore* store;
	ObjectMemoryPool<RecoverySubTask> task_queue;
	const std::string& from_store_ip;
	const std::string& from_ssd_uuid;
	replica_id_t rep_id;
	int from_store_id;
	int64_t recov_object_size;
	uint16_t meta_ver;
	lmt_entry* recovery_head_entry=NULL;
	void* pendding_buf = nullptr;
	PfBitmap recov_bmp;
	int64_t read_bs = RECOVERY_IO_SIZE;
	RecoveryContext(PfFlashStore* _store, const std::string& _from_store_ip,
		const std::string& _from_ssd_uuid,
		replica_id_t _rep_id,
		int _from_store_id,
		int64_t _recov_object_size,
		uint16_t _meta_ver):store(_store),from_store_ip(_from_store_ip), from_ssd_uuid(_from_ssd_uuid),
							rep_id(_rep_id), from_store_id(_from_store_id), recov_object_size(_recov_object_size), 
							meta_ver(_meta_ver), recov_bmp(int(_recov_object_size / SECTOR_SIZE))
	{

	}
	int init()
	{
		pendding_buf = app_context.recovery_buf_pool.alloc(recov_object_size, false); //size RECOVERY_IO_SIZE (128K), bug?
		if (pendding_buf == NULL) {
			S5LOG_ERROR("Failed to alloc pending buffer for recovery");
			return -ENOMEM;
		}

		recovery_head_entry = store->lmt_entry_pool.alloc();
		if(recovery_head_entry == NULL){
			S5LOG_ERROR("Failed to alloc heading entry");
			return -ENOMEM;

		}
		*recovery_head_entry = lmt_entry{ .offset = 0, //this is a placeholder entry, don't have valid position on disk
				.snap_seq = 0,
				.status = EntryStatus::RECOVERYING,
				.prev_snap = NULL,
				.waiting_io = NULL,
				.recovery_bmp = &recov_bmp,
				.recovery_buf = pendding_buf
		};


		

		return task_queue.init(iodepth);
	}
	~RecoveryContext()
	{
		if(recovery_head_entry)
			store->lmt_entry_pool.free(recovery_head_entry);
		if(pendding_buf)	
			app_context.recovery_buf_pool.free(pendding_buf, false);
	}
};
int PfFlashStore::recovery_replica(replica_id_t  rep_id, const std::string &from_store_ip, int32_t from_store_id,
								   const string& from_ssd_uuid, int64_t recov_object_size, uint16_t meta_ver)
{
	int rc;
	assert(pthread_self () != this->tid); //this function must run in different thread than this disk's proc thread
	Cleaner _clean;

	S5LOG_INFO("Begin recovery replica:0x%x, from:%s:%s => %s, at obj_size:%d", rep_id, from_store_ip.c_str(), from_ssd_uuid.c_str(),
			this->tray_name, recov_object_size);

	PfVolume* vol = NULL;
	{
		AutoMutexLock _l(&app_context.lock);
		auto pos = app_context.opened_volumes.find(rep_id.to_volume_id().vol_id);
		if (pos == app_context.opened_volumes.end()) {
			S5LOG_ERROR("volume 0x:%llx not opened", rep_id.to_volume_id().vol_id);
			return -EINVAL;
		}
		vol = pos->second;
		vol->add_ref();
	}
	DeferCall _d([vol] {vol->dec_ref(); });



	if(head.objsize != recov_object_size) {
		S5LOG_FATAL("Recovery between stores with different object size are not supported!");
	}

	

	int64_t shard_size = std::min<int64_t>(SHARD_SIZE, vol->size - rep_id.shard_index()*SHARD_SIZE);
	int64_t obj_cnt = shard_size/recov_object_size;

	RecoveryContext recov_ctx(this, from_store_ip, from_ssd_uuid, rep_id, from_store_id, recov_object_size, meta_ver);
	rc = recov_ctx.init();
	if(rc){
		S5LOG_ERROR("Failed to init RecoveryContext, rc:%d", rc);
		return rc;
	}
	for(int64_t i=0;i<obj_cnt;i++) {//recovery each object (in recov_object_size, not native object size)
		int64_t offset = ((int64_t)rep_id.shard_index() << SHARD_SIZE_ORDER) + recov_object_size * i;
		//set local object to recovery state
		int64_t obj_slba = vol_offset_to_block_slba(offset, head.objsize_order);
		lmt_key key = {.vol_id = rep_id.to_volume_id().vol_id, .slba = obj_slba, 0, 0};
		//S5LOG_DEBUG("recovery i:%d key:{.vol_id:0x%x, .slba:0x%x}", i, key.vol_id, key.slba);

		rc = recovery_object_series(recov_ctx,  key, offset);
		if(rc) {
			return rc;
		}
	}
	S5LOG_INFO("Finish recovery replica:0x%x, from:%s:%s => %s, at obj_size:%d", rep_id, from_store_ip.c_str(), from_ssd_uuid.c_str(),
	           this->tray_name, recov_object_size);

	return 0;
}
int PfFlashStore::recovery_object_series(struct RecoveryContext& recov_ctx, lmt_key &key, int64_t offset)
{
	int rc=0;
	recov_ctx.recovery_head_entry->recovery_bmp->clear();
	recov_ctx.recovery_head_entry->snap_seq = 0;
	recov_ctx.recovery_head_entry->status = EntryStatus::RECOVERYING;
	recov_ctx.recovery_head_entry->prev_snap = NULL;
	recov_ctx.recovery_head_entry->waiting_io = NULL;

	do{
		rc = this->sync_invoke([this, &key, &recov_ctx]()->int {
			auto block_pos = obj_lmt.find(key);
			if (block_pos == obj_lmt.end()) {
				obj_lmt[key] = recov_ctx.recovery_head_entry;
			}
			else {
				if (block_pos->second->status == RECOVERYING) {
					S5LOG_FATAL("Object already in recoverying, not handled!");
					//TODO: handle object already in recovering
				}
				if (block_pos->second->status == COPYING) {
					S5LOG_WARN("Object in COPYING state can't recovery!");
					//TODO: handle object in COPYING

					return -EBUSY;
				}
				recov_ctx.recovery_head_entry->prev_snap = block_pos->second;
				block_pos->second = recov_ctx.recovery_head_entry;
			}
			return 0;
			});
		if(rc == -EBUSY){
			S5LOG_INFO("Recovery target object in COW, wait it complete");
			sleep(1);
		}
	}while(rc != 0);

	GetSnapListReply primary_snap_list_reply;

	rc = query_store<GetSnapListReply>(recov_ctx.from_store_ip.c_str(), primary_snap_list_reply, "op=get_snap_list&volume_id=%lld&offset=%lld&ssd_uuid=%s",
		recov_ctx.rep_id.to_volume_id().vol_id, offset, recov_ctx.from_ssd_uuid.c_str());
	if (rc) {
		S5LOG_ERROR("Failed to query remote snap list from store:%s, rep_id:0x%llx, offset:%lld, reason:%s",
			recov_ctx.from_store_ip.c_str(), recov_ctx.rep_id.val(), offset, primary_snap_list_reply.reason.c_str());
		return rc;
	}
	vector<int>& primary_snap_list = primary_snap_list_reply.snap_list;
	vector<int> local_snap_list;
	rc = this->sync_invoke([this, &local_snap_list, offset, &recov_ctx]()->int {
		return get_snap_list(recov_ctx.rep_id.to_volume_id().vol_id, offset, local_snap_list);
		});
	if (rc) {
		S5LOG_ERROR("Failed to query local snap list, rep_id:0x%llx, offset:%lld, rc:%d",
			recov_ctx.rep_id.val(), offset, rc);
		return rc;
	}
	//S5LOG_INFO("lmt_key:%s has %d snaps[%s] on primary and %d on local [%s]", key.to_string().c_str(),
	//	primary_snap_list.size(), join(primary_snap_list).c_str(),
	//	local_snap_list.size(), join(local_snap_list).c_str());
	//delete unneeded object
	for (auto it = local_snap_list.begin(); it != local_snap_list.end(); ) {
		int snap = *it;
		int prev = -1;
		bool deleted = false;
		for (auto primary_it = primary_snap_list.rbegin(); primary_it != primary_snap_list.rend(); prev = *primary_it, ++primary_it) {
			if (snap > prev && snap < *primary_it) {
				//this snap not exists on primary
				S5LOG_INFO("delete object (rep_id:0x%llx offset:%lld snap:%d) because it's not exists on primary", recov_ctx.rep_id.val(), offset, snap);

				this->event_queue->sync_invoke([this, &key, snap]()->int {
					return delete_obj_by_snap_seq(&key, snap);
					});
				it = local_snap_list.erase(it);
				deleted = true;
				break;
			}
		}
		if (!deleted)
			++it;
	}



	//for each snap object not exists on local, recovery it
	//

	//S5LOG_DEBUG("filter primary snap list[%s] on local list[%s]", join(primary_snap_list).c_str(), join(local_snap_list).c_str());
	vector<int> snap_to_recovery;
	for (int s : primary_snap_list) {
		if (local_snap_list.size() < 2 || std::find(local_snap_list.begin() + 2, local_snap_list.end(), s) == local_snap_list.end()) {
			snap_to_recovery.push_back(s);
		}
	}


	int failed = 0;

	if(snap_to_recovery.size() > 0){
		S5LOG_INFO("recovering object (rep_id:0x%llx offset:%lld) %d objects[%s] to recover ", recov_ctx.rep_id.val(), offset,
			snap_to_recovery.size(), join(snap_to_recovery).c_str());
		//recovery each snapshot of this object
		for (auto snap_it = snap_to_recovery.rbegin(); snap_it != snap_to_recovery.rend(); ++snap_it) {
			uint32_t target_snap_seq = *snap_it;
			rc = recovery_single_object_entry(recov_ctx, key,  target_snap_seq, offset);
			if(rc){
				S5LOG_ERROR("recovery single obj entry failed, rc:%d", rc);
				failed ++;
				break;
			}

		}
	}
	int rc2 = finish_recovery_object(&key, recov_ctx.recovery_head_entry, recov_ctx.recov_object_size, offset, failed);
	if (rc || rc2) {
		S5LOG_ERROR("Failed recovery replica:0x%x, from:%s:%s => %s, at obj_size:%d", recov_ctx.rep_id, recov_ctx.from_store_ip.c_str(), recov_ctx.from_ssd_uuid.c_str(),
			recov_ctx.store->tray_name, recov_ctx.recov_object_size);
		return rc? : rc2;
	}
	return 0;
}


int PfFlashStore::recovery_single_object_entry(struct RecoveryContext& recov_ctx, lmt_key& key, uint32_t target_snap_seq, int64_t offset)
{
	int rc= 0;

	S5LOG_INFO("recovering object (rep_id:0x%llx offset:%lld snap: %d) ", recov_ctx.rep_id.val(), offset, target_snap_seq);

	lmt_entry* target_entry = recov_ctx.recovery_head_entry->prev_snap;
	for (; target_entry != NULL && target_entry->snap_seq != target_snap_seq; target_entry = target_entry->prev_snap);
	
	if (target_entry == NULL) {
		rc = this->event_queue->sync_invoke([this, &target_entry, target_snap_seq, &recov_ctx, key] {


			if (free_obj_queue.is_empty()) {
				S5LOG_ERROR("Failed to alloc object for recovery write, disk may be full. disk:%s", tray_name);
				return -ENOSPC;
			}
			int obj_id = free_obj_queue.dequeue();
			target_entry = lmt_entry_pool.alloc();
			*target_entry = lmt_entry{ offset: obj_id_to_offset(obj_id),
					snap_seq : target_snap_seq,
					status : EntryStatus::RECOVERYING,
					prev_snap : NULL,
					waiting_io : NULL
			};
			int rc2 = redolog->log_allocation(&key, target_entry, free_obj_queue.head);
			if (rc2)
			{
				lmt_entry_pool.free(target_entry);
				free_obj_queue.enqueue(obj_id);
				S5LOG_ERROR("Failed to log_allocation in recovery_write, rc:%d", rc2);
				return rc2;
			}
			rc2 = insert_lmt_entry_list(&recov_ctx.recovery_head_entry, target_entry,
				[target_snap_seq](lmt_entry* entry)->bool {
					if (entry->snap_seq == 0)
						return true;
					return entry->snap_seq > target_snap_seq;
				},

				[target_snap_seq](lmt_entry* entry)->bool {
					if (entry->snap_seq == 0)
						return false;
					return entry->snap_seq == target_snap_seq;
				}
				);
			if (rc2) {
				S5LOG_FATAL("Unexcepted error during insert recovering object");
			}
			return rc2;
		});
		// now we have two entry in RECOVERYING state
		//[Head placeholder RECOVERYING] -> [Entry RECOVERYING] -> [old entry ...]
	}
	else /* if (target_entry->snap_seq == target_snap_seq)*/ {

		//There's an existing target_entry, should we change it into status RECOVERYING?  No
	}
	if (rc)
		return rc;


	int failed = 0;
	sem_t recov_sem;
	sem_init(&recov_sem, 0, recov_ctx.iodepth);
	for (int j = 0; j < recov_ctx.recov_object_size / recov_ctx.read_bs && !failed; j++) {
		sem_wait(&recov_sem);
		RecoverySubTask* t = recov_ctx.task_queue.alloc();

		if (unlikely(t == NULL)) {
			S5LOG_FATAL("Unexpected error, can't alloc RecoverySubTask");
		}
		if (t->recovery_bd != NULL) {
			if (t->complete_status != PfMessageStatus::MSG_STATUS_SUCCESS) {
				S5LOG_ERROR("Previous recovery IO has failed, rc:%d", t->complete_status);
				failed = 1;
			}
			else {
				//TODO: call in asynchronous mode here,
				int rc2 = recovery_write(&key, target_entry, t->snap_seq, t->recovery_bd->buf, t->length, t->offset);
				if (rc2)
					failed = 1;
			}
			app_context.recovery_io_bd_pool.free(t->recovery_bd);
			t->recovery_bd = NULL;
			t->snap_seq = 0;
		}
		if (failed) {
			S5LOG_ERROR("prev recovery IO has failed");
			recov_ctx.task_queue.free(t);
			sem_post(&recov_sem);
			break;
		}

		BufferDescriptor* bd = app_context.recovery_io_bd_pool.alloc();
		if (bd == NULL) {
			S5LOG_ERROR("Failed to alloc recovery data bd");
			failed = 1;
			recov_ctx.task_queue.free(t);
			sem_post(&recov_sem);
			break;
		}
		bd->data_len = (int)recov_ctx.read_bs;
		t->recovery_bd = bd;
		t->volume_id = recov_ctx.rep_id.to_volume_id().vol_id;
		t->offset = offset + j * recov_ctx.read_bs;

		t->length = recov_ctx.read_bs;
		t->snap_seq = target_snap_seq;
		t->store_id = recov_ctx.from_store_id;
		t->rep_id = 0;//
		t->sem = &recov_sem;
		t->meta_ver = recov_ctx.meta_ver;
		t->opcode = PfOpCode::S5_OP_RECOVERY_READ;
		t->owner_queue = &recov_ctx.task_queue;
		app_context.replicators[0]->event_queue->post_event(EVT_RECOVERY_READ_IO, 0, t);
	}
	for (int i = 0; i < recov_ctx.iodepth; i++) {
		sem_wait(&recov_sem);
	}
	for (int i = 0; i < recov_ctx.iodepth; i++) {
		RecoverySubTask* t = recov_ctx.task_queue.alloc();
		if (t == NULL) {
			S5LOG_FATAL("Unexpected error, no RecoverySubTask in queue");
		}
		if (t->recovery_bd != NULL) {
			if (t->complete_status != PfMessageStatus::MSG_STATUS_SUCCESS) {
				S5LOG_ERROR("Previous recovery IO has failed, rc:%d", t->complete_status);
				failed = 1;
			}
			else {
				rc = recovery_write(&key, target_entry, t->snap_seq, t->recovery_bd->buf,
					t->length, t->offset);
				if (rc)
					failed = 1;
			}
			app_context.recovery_io_bd_pool.free(t->recovery_bd);
			t->recovery_bd = NULL;
			t->snap_seq = 0;
		}
		recov_ctx.task_queue.free(t);
	}
	//change target entry status to NORMAL
	rc = event_queue->sync_invoke([this, key, target_entry]()->int {
		if (target_entry->status != EntryStatus::NORMAL) {
			EntryStatus old = target_entry->status;
			target_entry->status = EntryStatus::NORMAL;
			return redolog->log_status_change(&key, target_entry, old);
		}
		return 0;
		});
	if (rc) {
		S5LOG_ERROR("Failed to write redolog rc:%d", rc);
		return rc;
	}
	sem_destroy(&recov_sem);
	rc = failed ? -EIO : rc;
	S5LOG_INFO("recovering object complete (rep_id:0x%llx offset:%lld snap: %d), rc:%d ", recov_ctx.rep_id.val(), offset, target_snap_seq, rc );
	 
	return rc;
}

int PfFlashStore::get_snap_list(volume_id_t volume_id, int64_t offset, vector<int>& snap_list)
{
	lmt_key key = {volume_id.vol_id, (int64_t)vol_offset_to_block_slba(offset, head.objsize_order), 0, 0};
	auto block_pos = obj_lmt.find(key);
	if(block_pos != obj_lmt.end()){
		lmt_entry* p = block_pos->second;
		if(p->status == EntryStatus::RECOVERYING && p->offset == 0) {
			//this is recovery head entry, skip it
			p =  p->prev_snap;
		}
		for(; p != NULL; p=p->prev_snap){
			snap_list.push_back(p->snap_seq);
		}
	}
	return 0;
}

int PfFlashStore::delete_replica(replica_id_t rep_id)
{
	int64_t obj_cnt = SHARD_SIZE/head.objsize;
	for(int64_t i=0;i<obj_cnt;i++) {
		int64_t offset = ((int64_t)rep_id.shard_index() << SHARD_SIZE_ORDER) + head.objsize * i;
		//set local object to recovery state
		int64_t obj_slba = vol_offset_to_block_slba(offset, head.objsize_order);
		lmt_key key = {.vol_id = rep_id.to_volume_id().vol_id, .slba = obj_slba, 0, 0};
		auto block_pos = obj_lmt.find(key);
		if (block_pos != obj_lmt.end()) {

			if (block_pos->second->status == RECOVERYING) {
				S5LOG_WARN(
						"Object(vol_id:0x%llx offset:%lld) is in recovering and not deleted, please run GC later to reclaim it!",
						key.vol_id, key.slba);
				continue;
			}
			lmt_entry *head = block_pos->second;
			while (head) {
				lmt_entry *p = head;
				head = head->prev_snap;
				delete_obj(&key, p);
			}
		}
	}
	return 0;
}

void PfFlashStore::trim_proc()
{
	char tname[32];
	snprintf(tname, sizeof(tname), "trim_%s", tray_name);
	prctl(PR_SET_NAME, tname);
	if (app_context.engine == SPDK)
		((PfspdkEngine *)ioengine)->pf_spdk_io_channel_open(1);

	while(1) {
		int total_cnt = 0;
		int MAX_CNT = 10;
		//TODO: implement trim logic
		for(total_cnt = 0; total_cnt < MAX_CNT; total_cnt ++) {
			int rc = event_queue->sync_invoke([this]() -> int {
				if (trim_obj_queue.is_empty())
					return -ENOENT;
				int id = trim_obj_queue.dequeue();
				free_obj_queue.enqueue(id);
				return redolog->log_free(id, trim_obj_queue.head, free_obj_queue.tail);
			});
			if(rc == -ENOENT)
				break;
		}
		if(total_cnt > 0){
			S5LOG_WARN("%d objects should be trimmed, but feature not implemented", total_cnt);
		}
		sleep(1);
	}
	if (app_context.engine == SPDK)
		((PfspdkEngine *)ioengine)->pf_spdk_io_channel_close(NULL);
}

void PfFlashStore::post_load_fix()
{
	//remove UNINITed entry
	for(auto it = obj_lmt.begin(); it != obj_lmt.end(); ){

		delete_matched_entry(&it->second,
			[](struct lmt_entry* _entry)->bool {
				return _entry->status == EntryStatus::COPYING || _entry->status == EntryStatus::RECOVERYING;
			},
			[this](struct lmt_entry* _entry)->void {
				lmt_entry_pool.free(_entry);
			});
		if (it->second == NULL){
			it = obj_lmt.erase(it);
		} else {
			++it;
		}
	}
}

void PfFlashStore::post_load_check()
{
	//TODO: check duplicate object id
	//an object should be referenced only once
	//all entry should in NORMAL or ERROR state, not COW, RECOVERYING
	//no duplicate snap_seq
	//snap seq not 0
	//used obj count + free + trim= total. any object should in one of three state: {used, free, trim}

	S5LOG_INFO("Begin post load check, %d keys in obj_lmt ...", obj_lmt.size());
	int error_cnt =0;
	S5LOG_INFO("Check for duplicated snap_seq and offset ...");
	for(auto it : obj_lmt){
		//S5LOG_INFO("check for key:%s", it.first.to_string().c_str());
		lmt_entry* head = it.second;
		int64_t last_off = head->offset;
		uint32_t last_snap = head->snap_seq;
		head = head->prev_snap;
		while(head!=nullptr){
			//S5LOG_INFO("\t snap:%d, off:0x%lx", head->snap_seq, head->offset);
			if(head->snap_seq == last_snap || head->offset == last_off){
				S5LOG_ERROR("duplicated snap_seq or offset. current (offset:0x%lx, snap:%u) vs last (0x%lx, %u)", 
					head->offset, head->snap_seq, last_off, last_snap);
				error_cnt++;
			}
			last_snap = head->snap_seq;
			last_off = head->offset;
			head=head->prev_snap;
		}
	}
	if(error_cnt){
		S5LOG_FATAL("%d errors found in metadata, can't continue");
	}
	S5LOG_INFO("Post check succeed!");
}


static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	return true;
}

static void
register_ns(struct spdk_nvme_ctrlr *ctrlr, struct ns_entry *entry)
{
	const struct spdk_nvme_ctrlr_data *cdata;
	//uint64_t ns_size;
	struct spdk_nvme_ns *ns = entry->ns;
	//uint32_t sector_size;

	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	if (!spdk_nvme_ns_is_active(ns)) {
		S5LOG_ERROR("Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
		       cdata->mn, cdata->sn,
		       spdk_nvme_ns_get_id(ns));		
		return;
	}

	//ns_size = spdk_nvme_ns_get_size(ns);
	//sector_size = spdk_nvme_ns_get_sector_size(ns);

	entry->ctrlr = ctrlr;
	entry->block_size = spdk_nvme_ns_get_extended_sector_size(ns);
	entry->block_cnt = spdk_nvme_ns_get_num_sectors(ns);
	entry->md_size = spdk_nvme_ns_get_md_size(ns);
	entry->md_interleave = spdk_nvme_ns_supports_extended_lba(ns);
	if (cdata->oncs.copy)
		entry->scc = true;
	else
		entry->scc = false;

	if (4096 % entry->block_size != 0) {
		S5LOG_ERROR("IO size is not a multiple of nsid %u sector size %u", spdk_nvme_ns_get_id(ns), entry->block_size);
		free(entry);
		return;
	}

	return ;
}

static void
register_ctrlr(struct spdk_nvme_ctrlr *ctrlr, struct ns_entry *entry)
{
	struct spdk_nvme_ns *ns;
	uint16_t nsid = entry->nsid;

	if (nsid == 0) {
		for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr);
		     nsid != 0; nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid)) {
			ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
			if (ns == NULL) {
				continue;
			}
			entry->nsid = nsid;
			entry->ns = ns;
			register_ns(ctrlr, entry);
			// only register fist ns
			break;
		}
	} else {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (!ns) {
			S5LOG_ERROR("Namespace does not exist");
			return ;
		}
		entry->ns = ns;
		register_ns(ctrlr, entry);
	}
}


static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	struct spdk_pci_addr	pci_addr;
	struct spdk_pci_device	*pci_dev;
	struct spdk_pci_id	pci_id;
	struct ns_entry *entry = (struct ns_entry*)cb_ctx;
	

	if (spdk_pci_addr_parse(&pci_addr, trid->traddr)) {
		return;
	}

	pci_dev = spdk_nvme_ctrlr_get_pci_device(ctrlr);
	if (!pci_dev) {
		return;
	}

	pci_id = spdk_pci_device_get_id(pci_dev);

	S5LOG_INFO("Attached to NVMe Controller at %s [%04x:%04x]\n",
			trid->traddr,
			pci_id.vendor_id, pci_id.device_id);

	register_ctrlr(ctrlr, entry);
}

int PfFlashStore::register_controller(const char *trid_str)
{
	struct spdk_nvme_transport_id	trid;
	const char *ns_str;
	char nsid_str[6];
	uint16_t nsid = 0;
	int len;
	
	trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
	if (spdk_nvme_transport_id_parse(&trid, trid_str) != 0) {
		S5LOG_ERROR("Invalid transport ID format %s", trid_str);
		return -EINVAL;
	}

	ns_str = strcasestr(trid_str, "ns:");
	if (ns_str) {
		ns_str += 3;
		len = (int)strcspn(ns_str, " \t\n");
		if (len > 5) {
			S5LOG_ERROR("Invalid NVMe namespace IDs len %d", len);
			return -EINVAL;
		}
		memcpy(nsid_str, ns_str, len);
		nsid_str[len] = '\0';
		nsid = (uint16_t)spdk_strtol(nsid_str, 10);
		if (nsid <= 0 || nsid > 65535) {
			S5LOG_ERROR("Invalid NVMe namespace ID=%d", nsid);
			return -EINVAL;
		}
	}

	ns = (struct ns_entry *)calloc(1, sizeof(struct ns_entry));
	if (ns == NULL) {
		S5LOG_ERROR("ns_entry malloc");
		return -ENOMEM;
	}
	ns->nsid = nsid;

	if (spdk_nvme_probe(&trid, ns, probe_cb, attach_cb, NULL) != 0) {
		S5LOG_ERROR("spdk_nvme_probe() failed for transport address: %s", trid_str);
		return -EINVAL;
	}
	return 0;
}

int disk_cnt = 0;
int PfFlashStore::spdk_nvme_init(const char *trid_str, uint16_t* p_id)
{
	int rc;
	int ret;
	char name_pool[8];

	sprintf(name_pool, "disk_%d", disk_cnt++);

	rc = register_controller(trid_str);
	if (rc) {
		S5LOG_ERROR("failed to register controller for %s", trid_str);
		return rc;
	}

	ioengine = new PfspdkEngine(this, ns);
	ioengine->init();
	((PfspdkEngine *)ioengine)->pf_spdk_io_channel_open(1);

	this->func_priv = ((PfspdkEngine *)ioengine)->poll_io;
	arg_v = (void *)((PfspdkEngine *)ioengine);

	safe_strcpy(this->tray_name, trid_str, sizeof(this->tray_name));

	PfEventThread::init(name_pool, MAX_AIO_DEPTH*2, *p_id);

	S5LOG_INFO("Spdk Loading tray %s ...", trid_str);

	/* 
	 Add md_lock，md_cond and compact variable initialization to the spdk_nvme_init function to solve the problem of the compact variable may have random values and md_lock getting stuck.
	*/
	pthread_mutex_init(&md_lock, NULL);
	pthread_cond_init(&md_cond, NULL);
	to_run_compact.store(COMPACT_IDLE);
	compact_lmt_exist = 0;

	if ((ret = read_store_head()) == 0)
	{
		ret = start_metadata_service(false);
	}
	else if (ret == -EUCLEAN)
	{
		ret = start_metadata_service(true);
	}
	else
		return ret;

	in_obj_offset_mask = head.objsize - 1;

	trimming_thread = std::thread(&PfFlashStore::trim_proc, this);

	((PfspdkEngine *)ioengine)->pf_spdk_io_channel_close(NULL);
	
	return ret;
}

SPDK_TRACE_REGISTER_FN(spdk_engine_trace, "spdk", TRACE_GROUP_SPDK)
{
	struct spdk_trace_tpoint_opts opts[] = {
        {
			"DISK_IO_STAT", TRACE_DISK_IO_STAT,
			OWNER_PFS_SPDK_IO, OBJECT_SPDK_IO, 1,
			{
				{ "lcost", SPDK_TRACE_ARG_TYPE_INT, 8},
				{ "icost", SPDK_TRACE_ARG_TYPE_INT, 8}
			}
		},
	};


	spdk_trace_register_owner(OWNER_PFS_SPDK_IO, 's');
	spdk_trace_register_object(OBJECT_SPDK_IO, 's');
	spdk_trace_register_description_ext(opts, SPDK_COUNTOF(opts));
}