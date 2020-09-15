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

#define CUT_LOW_10BIT(x) (((unsigned long)(x)) & 0xfffffffffffffc00L)
#define vol_offset_to_block_idx(offset, obj_size_order) ((offset) >> (obj_size_order))
#define offset_in_block(offset, in_obj_offset_mask) ((offset) & (in_obj_offset_mask))

#define OFFSET_HEAD 0
#define OFFSET_FREE_LIST 4096
#define OFFSET_TRIM_LIST (64LL<<20)
#define OFFSET_LMT_MAP (128LL<<20)
#define OFFSET_MD5 ((1LL<<30) - 4096)
#define OFFSET_META_COPY (1LL<<30)
#define OFFSET_REDO_LOG (2LL<<30)
#define REDO_LOG_SIZE (512LL<<20) //512M
static_assert(OFFSET_REDO_LOG + REDO_LOG_SIZE < MIN_META_RESERVE_SIZE, "OFFSET_REDO_LOG exceed reserve area");

static uint64_t get_device_cap(int fd);

struct CowTask : public IoSubTask{
	off_t src_offset;
	off_t dst_offset;
	void* buf;
	int size;
	sem_t sem;
};


static BOOL is_disk_clean(int fd)
{
	void *buf = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
	BOOL rc = TRUE;
	int64_t* p = (int64_t*)buf;

	if (-1 == pread(fd, buf, LBA_LENGTH, 0))
	{
		rc = FALSE;
		goto release1;
	}
	for (uint32_t i = 0; i < LBA_LENGTH / sizeof(int64_t); i++)
	{
		if (p[i] != 0)
		{
			rc = FALSE;
			goto release1;
		}
	}
release1:
	free(buf);
	return rc;
}
static int clean_meta_area(int fd, size_t size)
{
	size_t buf_len = 1 << 20;
	void *buf = aligned_alloc(LBA_LENGTH, buf_len);
	for(off_t off = 0; off < size; off += buf_len) {
		if(pwrite(fd, buf, buf_len, off) == -1){
			S5LOG_ERROR("Failed write zero to meta area, rc:%d", errno);
			return -errno;
		}
	}
	return 0;
}
/**
 * init flash store from tray. this function will create meta data
 * and initialize the tray if a tray is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  tray not exist or failed to open
 */

int PfFlashStore::init(const char* tray_name)
{
	int ret = 0;
	PfEventThread::init(tray_name, MAX_AIO_DEPTH*2);
	safe_strcpy(this->tray_name, tray_name, sizeof(this->tray_name));
	S5LOG_INFO("Loading disk %s ...", tray_name);
	Cleaner err_clean;
	fd = open(tray_name, O_RDWR|O_DIRECT);
	if (fd == -1)  {
		return -errno;
	}
	err_clean.push_back([this]() {::close(fd); });

	if ((ret = read_store_head()) == 0)
	{
		ret = load_meta_data();
		if (ret)
			return ret;
		redolog = new PfRedoLog();
		ret = redolog->load(this);
		if (ret)
		{
			S5LOG_ERROR("reodolog initialize failed rc:%d", ret);
			return ret;
		}
		ret = redolog->replay();
		if (ret)
		{
			S5LOG_ERROR("Failed to replay redo log, rc:%d", ret);
			return ret;
		}
		save_meta_data();
		S5LOG_INFO("Load block map, key:%d total obj count:%d free obj count:%d, in triming:%d", obj_lmt.size(),
		           free_obj_queue.queue_depth -1, free_obj_queue.count(), trim_obj_queue.count());

		redolog->start();
	}
	else if (ret == -EUCLEAN)
	{
		S5LOG_WARN("New disk found, initializing  (%s) now ...", tray_name);
		if(!is_disk_clean(fd))
		{
			S5LOG_ERROR("disk %s is not clean and will not be initialized.", tray_name);
			return ret;
		}
		size_t dev_cap = get_device_cap(fd);
		if(get_device_cap(fd) < (10LL<<30)) {
			S5LOG_WARN("Seems you are using a very small device with only %dGB capacity", dev_cap>>30);
		}
		ret = clean_meta_area(fd, app_context.meta_size);
		if(ret) {
			S5LOG_ERROR("Failed to clean meta area with zero, disk:%s, rc:%d", tray_name, ret);
			return ret;

		}
		if ((ret = initialize_store_head()) != 0)
		{
			S5LOG_ERROR("initialize_store_head failed rc:%d", ret);
			return ret;
		}
		int obj_count = (int) ((head.tray_capacity - head.meta_size) >> head.objsize_order);

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

		obj_lmt.reserve(obj_count * 2);
		redolog = new PfRedoLog();
		ret = redolog->init(this);
		if (ret)
		{
			S5LOG_ERROR("reodolog initialize failed ret(%d)", ret);
			return ret;
		}
		redolog->start();
		save_meta_data();
		S5LOG_INFO("Init new disk (%s) complete.", tray_name);
	}
	else
		return ret;

	in_obj_offset_mask = head.objsize - 1;
	init_aio();
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
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;

	lmt_key key = {io->rep->id, (int64_t)vol_offset_to_block_idx(cmd->offset, head.objsize_order), 0, 0 };
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
			io_prep_pread(&io->aio_cb, fd, data_bd->buf, cmd->length,
				entry->offset + offset_in_block(cmd->offset, in_obj_offset_mask));
			io->opcode = S5_OP_READ;
			struct iocb *ios[1] = {&io->aio_cb};
			io_submit(aio_ctx, 1, ios);
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
int PfFlashStore::do_write(IoSubTask* io)
{
	PfMessageHead* cmd = io->parent_iocb->cmd_bd->cmd_bd;
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	io->opcode = cmd->opcode;
	lmt_key key = {io->rep->id, (int64_t)vol_offset_to_block_idx(cmd->offset, head.objsize_order), 0, 0};
	auto block_pos = obj_lmt.find(key);
	lmt_entry *entry = NULL;

	if (block_pos == obj_lmt.end())
	{
		if (free_obj_queue.is_empty())
		{
			app_context.error_handler->submit_error(io, MSG_STATUS_NOSPACE);
			return 0;
		}
		int obj_id = free_obj_queue.dequeue();
		entry = lmt_entry_pool.alloc();
		*entry = lmt_entry { offset: obj_id_to_offset(obj_id),
			snap_seq : cmd->snap_seq,
			status : EntryStatus::NORMAL,
			prev_snap : NULL,
			waiting_io : NULL
		};
		obj_lmt[key] = entry;
		int rc = redolog->log_allocation(&key, entry, free_obj_queue.head);
		if (rc)
		{
			app_context.error_handler->submit_error(io, MSG_STATUS_LOGFAILED);
			S5LOG_ERROR("log_allocation error, rc:%d", rc);
			return 0;
		}

	}
	else
	{
		entry = block_pos->second;

		if(likely(cmd->snap_seq == entry->snap_seq)) {
			if (unlikely(entry->status != EntryStatus::NORMAL))
			{
				if(entry->status == EntryStatus::COPYING) {
					io->next = entry->waiting_io;
					entry->waiting_io = io; //insert io to waiting list
					return 0;
				}
				S5LOG_FATAL("Block in abnormal status:%d", entry->status);
				io->complete(MSG_STATUS_INTERNAL);
				return -EINVAL;
			}

		} else if (unlikely(cmd->snap_seq < entry->snap_seq)) {
			S5LOG_ERROR("Write on snapshot not allowed! vol_id:0x%x request snap:%d, target snap:%d",
				cmd->vol_id, cmd->snap_seq , entry->snap_seq);
			io->complete(MSG_STATUS_READONLY);
			return 0;
		} else if(unlikely(cmd->snap_seq > entry->snap_seq)) {
			if (free_obj_queue.is_empty())
			{
				app_context.error_handler->submit_error(io, MSG_STATUS_NOSPACE);
				return -ENOSPC;
			}
			int obj_id = free_obj_queue.dequeue();
			struct lmt_entry* dstEntry = lmt_entry_pool.alloc();
			*dstEntry = lmt_entry { offset: obj_id_to_offset(obj_id),
					snap_seq : cmd->snap_seq,
					status : EntryStatus::COPYING,
					prev_snap : entry,
					waiting_io : NULL
			};
			obj_lmt[key] = dstEntry;
			int rc = redolog->log_allocation(&key, dstEntry, free_obj_queue.head);
			if (rc)
			{
				app_context.error_handler->submit_error(io, MSG_STATUS_LOGFAILED);
				S5LOG_ERROR("log_allocation error, rc:%d", rc);
				return -EIO;
			}
			io->next = dstEntry->waiting_io;
			dstEntry->waiting_io = io; //insert io to waiting list
			S5LOG_DEBUG("Call begin_cow for io:%p, entry:%p dstEntry:%p", io, entry, dstEntry);
			begin_cow(&key, entry, dstEntry);
			return 0;
		}


	}
	//below is the most possible case
	io_prep_pwrite(&io->aio_cb, fd, data_bd->buf, cmd->length,
	               entry->offset + offset_in_block(cmd->offset, in_obj_offset_mask));
	struct iocb* ios[1] = {&io->aio_cb};
	//S5LOG_DEBUG("io_submit for cid:%d, ssd:%s, len:%d", cmd->command_id, tray_name, cmd->length);
	io_submit(aio_ctx, 1, ios);
	return 0;
}

static uint64_t get_device_cap(int fd)
{
	struct stat fst;
	int rc = fstat(fd, &fst);
	if(rc != 0)
	{
		rc = -errno;
		S5LOG_ERROR("Failed fstat, rc:%d", rc);
		return rc;
	}
	if(S_ISBLK(fst.st_mode )){
		long number;
		ioctl(fd, BLKGETSIZE, &number);
		return number*512;
	}
	else
	{
		return fst.st_size;
	}
	return 0;
}

int PfFlashStore::initialize_store_head()
{
	memset(&head, 0, sizeof(head));
	char uuid_str[64];
	head.magic = 0x3553424e; //magic number, NBS5
	head.version= S5_VERSION; //S5 version
	uuid_generate(head.uuid);
	uuid_unparse(head.uuid, uuid_str);
	S5LOG_INFO("generate disk uuid:%s", uuid_str);
	head.key_size=sizeof(lmt_key);
	head.entry_size=sizeof(lmt_entry);
	head.objsize=OBJ_SIZE;
	head.objsize_order=OBJ_SIZE_ORDER; //objsize = 2 ^ objsize_order
	head.tray_capacity = get_device_cap(fd);
	head.meta_size = app_context.meta_size;
	head.free_list_position = OFFSET_FREE_LIST;
	head.free_list_size = (64 << 20) - 4096;
	head.trim_list_position = OFFSET_TRIM_LIST;
	head.trim_list_size = 64 << 20;
	head.lmt_position = OFFSET_LMT_MAP;
	head.lmt_size = 512 << 20;
	head.metadata_md5_position = OFFSET_MD5;
	head.head_backup_position = OFFSET_META_COPY;
	head.redolog_position = OFFSET_REDO_LOG;
	head.redolog_size = REDO_LOG_SIZE;
	time_t time_now = time(0);
	strftime(head.create_time, sizeof(head.create_time), "%Y%m%d %H:%M:%S", localtime(&time_now));


	void *buf = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
	if(!buf)
	{
		S5LOG_ERROR("Failed to alloc memory");
		return -ENOMEM;
	}
	memset(buf, 0, LBA_LENGTH);
	memcpy(buf, &head, sizeof(head));
	int rc = 0;
	if (-1 == pwrite(fd, buf, LBA_LENGTH, 0))
	{
		rc = -errno;
		goto release1;
	}
release1:
	free(buf);
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
	int rc = md5_stream->read(buf, buf_size, offset);
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
	int rc = md5_stream->write(buf, buf_size, offset);
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
	if (-1 == stream->write(buf, LBA_LENGTH, offset))
	{
		return -errno;
	}
	size_t src = 0;
	while(src < q->queue_depth*sizeof(T))
	{
		memset(buf, 0, buf_size);
		size_t s = std::min(q->queue_depth * sizeof(T) - src, (size_t)buf_size);
		memcpy(buf, ((char*)q->data) + src, s);
		if (-1 == stream->write(buf, up_align(s, LBA_LENGTH), offset + src + LBA_LENGTH))
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
	int rc = stream->read(buf, LBA_LENGTH, offset);
	if (rc != 0)
		return rc;
	int* buf_as_int = (int*)buf;
	rc = q->init(buf_as_int[0]);
	if (rc)
		return rc;

	q->head = buf_as_int[1];
	q->tail = buf_as_int[2];

	size_t src = 0;
	while (src < q->queue_depth * sizeof(T))
	{
		memset(buf, 0, buf_size);
		unsigned long s = std::min(q->queue_depth * sizeof(T) - src, (size_t)buf_size);
		if (-1 == stream->read(buf, up_align(s, LBA_LENGTH), offset + src + LBA_LENGTH))
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
*/
int PfFlashStore::save_meta_data()
{
	int buf_size = 1 << 20;
	void* buf = aligned_alloc(LBA_LENGTH, buf_size);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in save_meta_data");
		return -ENOMEM;
	}
	DeferCall _c([buf]()->void {
		free(buf);
	});
	int rc = 0;
	MD5Stream stream(fd);
	rc = stream.init();
	if (rc) return rc;
	rc = save_fixed_queue<int32_t>(&free_obj_queue, &stream, head.free_list_position, (char*)buf, buf_size);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed to save free obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = save_fixed_queue<int32_t>(&trim_obj_queue, &stream, head.trim_list_position, (char*)buf, buf_size);
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
	if (-1 == stream.write(buf, LBA_LENGTH, head.lmt_position))
	{
		rc = -errno;
		S5LOG_ERROR("Failed to save lmt head, disk:%s rc:%d", tray_name, rc);
		return rc;
	}

	LmtEntrySerializer ser(head.lmt_position + LBA_LENGTH, buf, buf_size, true, &stream);
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
	stream.finalize(head.metadata_md5_position, 0);
	redolog->discard();
	return 0;
}

int PfFlashStore::load_meta_data()
{
	int buf_size = 1 << 20;
	void* buf = aligned_alloc(LBA_LENGTH, buf_size);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in save_meta_data");
		return -ENOMEM;
	}
	DeferCall _c([buf]() {
		free(buf);
	});

	int rc = 0;
	MD5Stream stream(fd);
	rc = stream.init();
	if (rc)
	{
		S5LOG_ERROR("Failed to init md5 stream, disk:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = load_fixed_queue<int32_t>(&free_obj_queue, &stream, head.free_list_position, (char*)buf, buf_size);
	if(rc)
	{
		S5LOG_ERROR("Failed to load free obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = load_fixed_queue<int32_t>(&trim_obj_queue, &stream, head.trim_list_position, (char*)buf, buf_size);
	if (rc)
	{
		S5LOG_ERROR("Failed to load trim obj queue, disk:%s rc:%d", tray_name, rc);
		return rc;
	}

	rc = lmt_entry_pool.init(free_obj_queue.queue_depth * 2);
	if (rc)
	{
		S5LOG_ERROR("Failed to init lmt_entry_pool, disk:%s rc:%d", tray_name, rc);
		return rc;
	}

	uint64_t obj_count = (head.tray_capacity - head.meta_size) >> OBJ_SIZE_ORDER;
	obj_lmt.reserve(obj_count * 2);
	if (stream.read(buf, LBA_LENGTH, head.lmt_position) == -1)
	{
		rc = -errno;
		S5LOG_ERROR("read block entry head failed rc:%d", rc);
	}
	int *buf_as_int = (int*)buf;

	int key_count = buf_as_int[0];

	LmtEntrySerializer reader(head.lmt_position + LBA_LENGTH, buf, buf_size, 0, &stream);
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
			tail->prev_snap = b;
			rc = reader.read_entry(tail->prev_snap);
			if (rc) {
				S5LOG_ERROR("Failed to read entry.");
				return rc;
			}
			tail = tail->prev_snap;
		}
		obj_lmt[k] = head_entry;
	}
	/*just for update md5*/
	if (key_count == 0)
		reader.load_buffer();
	S5LOG_INFO("Load block map, key:%d total obj count:%d free obj count:%d", key_count,
		 free_obj_queue.queue_depth -1, free_obj_queue.count());

	return 0;

}

int PfFlashStore::read_store_head()
{
	char uuid_str[64];
	void* buf = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in read_store_head");
		return -ENOMEM;
	}
	DeferCall([buf]() {
		free(buf);
	});
	if (-1 == pread(fd, buf, LBA_LENGTH, 0))
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


int PfFlashStore::process_event(int event_type, int arg_i, void* arg_p)
{
	switch (event_type) {
	case EVT_IO_REQ:
	{
	    PfOpCode op = ((IoSubTask*)arg_p)->parent_iocb->cmd_bd->cmd_bd->opcode;
        switch(op){
            case PfOpCode::S5_OP_READ:
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
		io_prep_pread(&req->aio_cb, fd, req->buf, req->size, req->src_offset );
		struct iocb* ios[1] = {&req->aio_cb};
		S5LOG_DEBUG("io_submit for cow read, ssd:%s, buf:%p len:%d  offset:0x%llx", this->tray_name, req->buf,
			  req->size, req->src_offset);
		io_submit(aio_ctx, 1, ios);
	}
	break;
	case EVT_COW_WRITE:
	{
		struct CowTask *req = (struct CowTask*)arg_p;
		req->opcode = S5_OP_COW_WRITE;
		io_prep_pwrite(&req->aio_cb, fd, req->buf, req->size, req->dst_offset );
		struct iocb* ios[1] = {&req->aio_cb};
		//S5LOG_DEBUG("io_submit for cid:%d, ssd:%s, len:%d", cmd->command_id, tray_name, cmd->length);
		io_submit(aio_ctx, 1, ios);
	}
	break;
	default:
		S5LOG_FATAL("Unimplemented event type:%d", event_type);
	}
    return 0;
}

void PfFlashStore::aio_polling_proc()
{
#define MAX_EVT_CNT 100
	struct io_event evts[MAX_EVT_CNT];
	char name[32];
	snprintf(name, sizeof(name), "aio_%s", tray_name);
	prctl(PR_SET_NAME, name);
	int rc=0;
	while(1) {
		rc = io_getevents(aio_ctx, 1, MAX_EVT_CNT, evts, NULL);
		if(rc < 0)
		{
			continue;
		}
		else
		{
			for(int i=0;i<rc;i++)
			{
				struct iocb* aiocb = (struct iocb*)evts[i].obj;
				int64_t len = evts[i].res;
				int64_t res = evts[i].res2;
				IoSubTask* t = pf_container_of(aiocb, IoSubTask, aio_cb);
				switch(t->opcode) {
					case S5_OP_READ:
					case S5_OP_WRITE:
					case S5_OP_REPLICATE_WRITE:
						//S5LOG_DEBUG("aio complete, cid:%d len:%d rc:%d", t->parent_iocb->cmd_bd->cmd_bd->command_id, (int)len, (int)res);
						if (unlikely(len != t->parent_iocb->cmd_bd->cmd_bd->length || res < 0)) {
							S5LOG_ERROR("aio error, len:%d rc:%d", (int) len, (int) res);
							//res = (res == 0 ? len : res);
							app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);
						} else
							t->complete(PfMessageStatus::MSG_STATUS_SUCCESS);
						break;
					case S5_OP_COW_READ:
					case S5_OP_COW_WRITE:
						if (unlikely(len != ((CowTask *) t)->size || res < 0)) {
							S5LOG_ERROR("cow aio error, op:%d, len:%d rc:%d", t->opcode, (int) len, (int) res);
							//res = (res == 0 ? len : res);
							app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);

						} else {
							t->complete_status = PfMessageStatus::MSG_STATUS_SUCCESS;
							sem_post(&((CowTask*)t)->sem);
						}

						break;
					default:
						S5LOG_FATAL("Unknown task opcode:%d", t->opcode);
				}

			}
		}
	}
}

void PfFlashStore::init_aio()
{
    int rc = io_setup(MAX_AIO_DEPTH, &aio_ctx);
    if(rc < 0)
    {
        S5LOG_ERROR("io_setup failed, rc:%d", rc);
        throw std::runtime_error(format_string("io_setup failed, rc:%d", rc));
    }
    aio_poller = std::thread(aio_polling_proc, this);
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
{
	CowTask r;
	r.src_offset = srcEntry->offset;
	r.dst_offset = dstEntry->offset;
	r.size = COW_OBJ_SIZE;
	sem_init(&r.sem, 0, 0);

	r.buf = app_context.cow_buf_pool.alloc(COW_OBJ_SIZE);
	event_queue.post_event(EVT_COW_READ, 0, &r);
	sem_wait(&r.sem);
	if(unlikely(r.complete_status != PfMessageStatus::MSG_STATUS_SUCCESS))	{
		goto cowfail;
	}

	event_queue.post_event(EVT_COW_WRITE, 0, &r);
	sem_wait(&r.sem);
	if(unlikely(r.complete_status != PfMessageStatus::MSG_STATUS_SUCCESS))	{
		goto cowfail;
	}
	sync_invoke([key, srcEntry, dstEntry, this]()->int {
		dstEntry->status = EntryStatus::NORMAL;
		IoSubTask *t = dstEntry->waiting_io;
		dstEntry->waiting_io = NULL;
		while (t) {
			if (unlikely(t->opcode != S5_OP_WRITE && t->opcode != S5_OP_REPLICATE_WRITE)) {
				S5LOG_FATAL("Unexcepted op code:%d", t->opcode);
			}

			do_write(t);
			t = t->next;
		}
		if(srcEntry->status == DELAY_DELETE_AFTER_COW) {
			delete_obj(key, srcEntry);
		}
		return 0;
	});
	app_context.cow_buf_pool.free(r.buf);
	return;
cowfail:
	sync_invoke([dstEntry]()->int
	{
		IoSubTask* t = dstEntry->waiting_io;
		dstEntry->waiting_io = NULL;
		while (t) {
			t->complete(PfMessageStatus::MSG_STATUS_REOPEN);
			t=t->next;
		}
		return 0;
	});
	app_context.cow_buf_pool.free(r.buf);
	return;
}

void PfFlashStore::delete_snapshot(shard_id_t shard_id, uint32_t snap_seq_to_del, uint32_t prev_snap_seq, uint32_t next_snap_seq) {
	uint64_t shard_index = SHARD_INDEX(shard_id.val());
	uint64_t vol_id = VOLUME_ID(shard_id.val());
	int obj_lba_cnt = (int)(head.objsize / LBA_LENGTH);
	for(int i=0; i<SHARD_LBA_CNT; i += obj_lba_cnt)
	{
		uint64_t start_lba = shard_index * SHARD_LBA_CNT + i;
		delete_obj_snapshot(vol_id, start_lba, snap_seq_to_del, prev_snap_seq, next_snap_seq);
	}

}

int PfFlashStore::delete_obj_snapshot(uint64_t volume_id, int64_t slba, uint32_t snap_seq, uint32_t prev_snap_seq, uint32_t next_snap_seq)
{
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
		S5LOG_WARN("delete_obj_snapshot aborted, for object(vol_id:0x%llx slba:%lld", volume_id, slba);
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
	 *   C3) keet it, not change its snap sequence
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
		redolog->log_snap_seq_change(&key, target_entry, target_entry->snap_seq);
		target_entry->snap_seq = Lns;
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

		redolog->log_snap_seq_change(&key, target_entry, target_entry->snap_seq);
		target_entry->snap_seq = Lns;
	}
	else if( (Pps <= Lps) /*A2*/  && next_entry == NULL /*B3*/) {
		/* 	=case A2 B3=
	   *                         Lps                  Lts           Lns
	   *                         |                    |             |
	   *                         |                    |             |
	   * Logical    -------------Sp-------------------D-------------Sn------->
	   *
	   *
	   *	     	        +--+        +--+
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
		redolog->log_snap_seq_change(&key, target_entry, target_entry->snap_seq);
		target_entry->snap_seq = Lps;
	}
	else {
		S5LOG_FATAL("del snapshot encounter unexpected state: vol_id:0x%llx slba:%lld Lps:%d Lts:%d Lns:%d Pps:%d Pts:%d Pns:%d",
			  volume_id, slba, Lps, Lts, Lns, Pps, Pts, Pns);
	}
	return 0;
}


static void delete_matched_entry(struct lmt_entry **head_ref, std::function<bool(struct lmt_entry *)> match,
		std::function<void(struct lmt_entry *)> free_func)
{
	// Store head node
	struct lmt_entry* temp = *head_ref, *prev;

	// If head node itself holds the key or multiple occurrences of key
	while (temp != NULL && match(temp))
	{
		*head_ref = temp->prev_snap;   // Changed head
		free_func(temp);    // free old head
		temp = *head_ref;         // Change Temp
	}

	// Delete occurrences other than head
	while (temp != NULL)
	{
		// Search for the key to be deleted, keep track of the
		// previous node as we need to change 'prev->next'
		while (temp != NULL && !match(temp))
		{
			prev = temp;
			temp = temp->prev_snap;
		}

		// If key was not present in linked list
		if (temp == NULL) return;

		// Unlink the node from linked list
		prev->prev_snap = temp->prev_snap;

		free_func(temp);  // Free memory

		//Update Temp for next iteration of outer loop
		temp = prev->prev_snap;
	}
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
	                     });
	if(pos->second == NULL)
		obj_lmt.erase(pos);
	return 0;
}
