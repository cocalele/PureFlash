/**
 * Copyright (C), 2019.
 * @endcode GBK
 *
 * @file this file include comments in Chinese. please open this file with UTF-8 encoding.
 * flash_store 就是一个存储单元，即，一个SSD或者flash卡。flash_store这个类的功能包括：
 *  1. 初始化Store， 如果一个SSD是干净的未经初始化的，那么就对起初始化。如果这个盘上面有数据，就检查是不是一个
 *     合法的Store，如果是就进行Load过程。
 *  2. 注册store, 将store信息注册到zookeeper里面
 *  3. 接收IO请求，Dispatcher会将IO发送到Store线程，Store线程用libaio接口将IO下发。
 *  4. polling IO，下发到SSD或者flash卡的IO完成后，由aio poller线程处理
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

#include "afs_flash_store.h"
#include "s5_utils.h"
#include "afs_server.h"
#include "s5_log.h"
#include "s5message.h"
#include "afs_cluster.h"
#include "s5_md5.h"
#include "s5_redolog.h"
#include "s5_block_tray.h"

#define CUT_LOW_10BIT(x) (((unsigned long)(x)) & 0xfffffffffffffc00L)

#define OFFSET_HEAD 0
#define OFFSET_FREE_LIST 4096
#define OFFSET_TRIM_LIST (64LL<<20)
#define OFFSET_LMT_MAP (128LL<<20)
#define OFFSET_MD5 ((1LL<<30) - 4096)
#define OFFSET_META_COPY (1LL<<30)
#define OFFSET_REDO_LOG (2LL<<30)

static BOOL is_disk_clean(Tray *tray)
{
	void *buf = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
	BOOL rc = TRUE;
	int64_t* p = (int64_t*)buf;

	if (-1 == tray->sync_read(buf, PAGE_SIZE, 0))
	{
		rc = FALSE;
		goto release1;
	}
	for (uint32_t i = 0; i < PAGE_SIZE / sizeof(int64_t); i++)
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

/**
 * init flash store from tray. this function will create meta data
 * and initialize the tray if a tray is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  tray not exist or failed to open
 */

int S5FlashStore::init(const char* tray_name)
{
	int ret = 0;
	safe_strcpy(this->tray_name, tray_name, sizeof(this->tray_name));
	S5LOG_INFO("Loading tray (%s) ...", tray_name);
	tray = new BlockTray();
	ret = tray->init(tray_name);
	if (ret == -1)  {
		return -errno;
	}

	if ((ret = read_store_head()) == 0)
	{
		ret = load_meta_data();
		if (ret)
			goto error1;
		S5LOG_INFO("Load tray (%s) complete.", tray_name);
	}
	else if (ret == -EUCLEAN)
	{
		S5LOG_WARN("New tray found, initializing  (%s) now ...", tray_name);
		if(!is_disk_clean(tray))
		{
			S5LOG_ERROR("tray %s is not clean and will not be initialized.", tray_name);
			goto error1;
		}
		if ((ret = initialize_store_head()) != 0)
		{
			S5LOG_ERROR("initialize_store_head failed rc:%d", ret);
			goto error1;
		}
		int obj_count = (int) ((head.tray_capacity - head.meta_size) >> head.objsize_order);

		ret = free_obj_queue.init(obj_count);
		if (ret)
		{
			S5LOG_ERROR("free_obj_queue initialize failed ret(%d)", ret);
			goto error1;
		}
		for (int i = 0; i < obj_count; i++)
		{
			free_obj_queue.enqueue(i);
		}
		ret = trim_obj_queue.init(obj_count);
		if (ret)
		{
			S5LOG_ERROR("trim_obj_queue initialize failed ret(%d)", ret);
			goto error1;
		}

		obj_lmt.reserve(obj_count * 2);
		redolog = new S5RedoLog();
		ret = redolog->init(this);
		if (ret)
		{
			S5LOG_ERROR("reodolog initialize failed ret(%d)", ret);
			goto error1;
		}
		save_meta_data();
		S5LOG_INFO("Init new tray (%s) complete.", tray_name);
	}
	else
		goto error1;
	return ret;

error1:
	tray->destroy();
	delete tray;
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
int S5FlashStore::read(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, /*out*/char* buf)
{
	//int64_t slba_aligned = (int64_t)CUT_LOW_10BIT(slba);
	//struct lmt_key key={vol_id, slba_aligned};
	//size_t vs;
	//auto it = obj_lmt.find(key);
	//if(it == obj_lmt.end())
	//	return -ENOENT;

	//int rc = (int)pread(dev_fd, buf, (size_t)nlba<<LBA_LENGTH_ORDER, it->second->offset + ((slba%1024) << LBA_LENGTH_ORDER));
	//if(rc == -1)
	//	return -ENOMEM;
	//return rc;
	return 0;
}

/**
 * write data to flash store.
 * a LBA is a block of data 4096 bytes.
 * @return number of lbas has written to store
 *         negative value for error
 */

int S5FlashStore::write(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, char* buf)
{
	//int64_t slba_aligned = (int64_t)CUT_LOW_10BIT(slba);
	//struct lmt_key key={vol_id, slba_aligned};
	//size_t vs;
	//int64_t offset;
	//auto it = obj_lmt.find(key);
	//if (it == obj_lmt.end())
	//{
	//	if (free_obj_queue.is_empty())
	//		return -ENOSPC;
	//	int obj_idx = free_obj_queue.dequeue();
	//	struct lmt_entry *new_entry = lmt_entry_pool.alloc();
	//	if (new_entry == NULL)
	//		throw std::logic_error("No lmt entry to alloc");
	//	*new_entry = { (obj_idx << OBJ_SIZE_ORDER) + meta_size, snap_seq };
	//	obj_lmt[key] = new_entry;
	//	offset = new_entry->offset + ((slba % 1024) << LBA_LENGTH_ORDER);
	//}
	//else
	//	offset = it->second->offset + ((slba % 1024) << LBA_LENGTH_ORDER);
	//int rc = (int)pwrite(dev_fd, buf, (size_t)nlba<<LBA_LENGTH_ORDER, offset);
	//if(rc == -1)
	//	return -ENOMEM;
	return 0;
}

int S5FlashStore::initialize_store_head()
{
	memset(&head, 0, sizeof(head));
	long numblocks;
	if(tray->get_num_blocks(&numblocks))
	{
		S5LOG_ERROR("Failed to get tray:%s size, rc:%d", tray_name, -errno);
		return -errno;
	}
	head.magic = 0x3553424e; //magic number, NBS5
	head.version= S5_VERSION; //S5 version
	uuid_generate(head.uuid);

	head.key_size=sizeof(lmt_key);
	head.entry_size=sizeof(lmt_entry);
	head.objsize=OBJ_SIZE;
	head.objsize_order=OBJ_SIZE_ORDER; //objsize = 2 ^ objsize_order
	head.tray_capacity = numblocks << 9;//512 byte per block
	head.meta_size = META_RESERVE_SIZE;
	head.free_list_position = OFFSET_FREE_LIST;
	head.free_list_size = (64 << 20) - 4096;
	head.trim_list_position = OFFSET_TRIM_LIST;
	head.trim_list_size = 64 << 20;
	head.lmt_position = OFFSET_LMT_MAP;
	head.lmt_size = 512 << 20;
	head.metadata_md5_position = OFFSET_MD5;
	head.head_backup_position = OFFSET_META_COPY;
	head.redolog_position = OFFSET_REDO_LOG;
	head.redolog_size = 512 << 20;
	time_t time_now = time(0);
	strftime(head.create_time, sizeof(head.create_time), "%Y%m%d %H:%M:%S", localtime(&time_now));


	void *buf = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
	if(!buf)
	{
		S5LOG_ERROR("Failed to alloc memory");
		return -ENOMEM;
	}
	memset(buf, 0, PAGE_SIZE);
	memcpy(buf, &head, sizeof(head));
	int rc = 0;
	if (-1 == tray->sync_write(buf, sizeof(PAGE_SIZE), 0))
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
static int save_fixed_queue(S5FixedSizeQueue<T>* q, MD5Stream* stream, off_t offset, char* buf, int buf_size)
{
	memset(buf, 0, buf_size);
	int* buf_as_int = (int*)buf;
	buf_as_int[0] = q->queue_depth;
	buf_as_int[1] = q->head;
	buf_as_int[2] = q->tail;
	if (-1 == stream->write(buf, PAGE_SIZE, offset))
	{
		return -errno;
	}
	size_t src = 0;
	while(src < q->queue_depth*sizeof(T))
	{
		memset(buf, 0, buf_size);
		size_t s = std::min(q->queue_depth * sizeof(T) - src, (size_t)buf_size);
		memcpy(buf, ((char*)q->data) + src, s);
		if (-1 == stream->write(buf, up_align(s, PAGE_SIZE), offset + src + PAGE_SIZE))
		{
			return -errno;
		}
		src += s;
	}
	return 0;
}

template<typename T>
static int load_fixed_queue(S5FixedSizeQueue<T>* q, MD5Stream* stream, off_t offset, char* buf, int buf_size)
{
	int rc = stream->read(buf, PAGE_SIZE, offset);
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
		if (-1 == stream->read(buf, up_align(s, PAGE_SIZE), offset + src + PAGE_SIZE))
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
int S5FlashStore::save_meta_data()
{
	int buf_size = 1 << 20;
	void* buf = aligned_alloc(PAGE_SIZE, buf_size);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in save_meta_data");
		return -ENOMEM;
	}
	DeferCall _c([buf]()->void {
		free(buf);
	});
	int rc = 0;
	MD5Stream stream(tray);
	rc = stream.init();
	if (rc) return rc;
	rc = save_fixed_queue<int32_t>(&free_obj_queue, &stream, head.free_list_position, (char*)buf, buf_size);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed to save free obj queue, tray:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = save_fixed_queue<int32_t>(&trim_obj_queue, &stream, head.trim_list_position, (char*)buf, buf_size);
	if (rc != 0)
	{
		S5LOG_ERROR("Failed to save trim obj queue, tray:%s rc:%d", tray_name, rc);
		return rc;
	}

	memset(buf, 0, buf_size);
	int* buf_as_int = (int*)buf;
	buf_as_int[0] = (int)obj_lmt.size();
	buf_as_int[1] = (int)sizeof(struct lmt_entry);
	buf_as_int[2] = 0;
	if (-1 == stream.write(buf, PAGE_SIZE, head.lmt_position))
	{
		rc = -errno;
		S5LOG_ERROR("Failed to save lmt head, tray:%s rc:%d", tray_name, rc);
		return rc;
	}

	LmtEntrySerializer ser(head.lmt_position + PAGE_SIZE, buf, buf_size, true, &stream);
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
	return 0;
}

int S5FlashStore::load_meta_data()
{
	int buf_size = 1 << 20;
	void* buf = aligned_alloc(PAGE_SIZE, buf_size);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in save_meta_data");
		return -ENOMEM;
	}
	DeferCall _c([buf]() {
		free(buf);
	});

	int rc = 0;
	MD5Stream stream(tray);
	rc = stream.init();
	if (rc)
	{
		S5LOG_ERROR("Failed to init md5 stream, tray:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = load_fixed_queue<int32_t>(&free_obj_queue, &stream, head.free_list_position, (char*)buf, buf_size);
	if(rc)
	{
		S5LOG_ERROR("Failed to load free obj queue, tray:%s rc:%d", tray_name, rc);
		return rc;
	}
	rc = load_fixed_queue<int32_t>(&trim_obj_queue, &stream, head.trim_list_position, (char*)buf, buf_size);
	if (rc)
	{
		S5LOG_ERROR("Failed to load trim obj queue, tray:%s rc:%d", tray_name, rc);
		return rc;
	}

	rc = lmt_entry_pool.init(free_obj_queue.queue_depth * 2);
	if (rc)
	{
		S5LOG_ERROR("Failed to init lmt_entry_pool, tray:%s rc:%d", tray_name, rc);
		return rc;
	}

	uint64_t obj_count = (head.tray_capacity - head.meta_size) >> OBJ_SIZE_ORDER;
	obj_lmt.reserve(obj_count * 2);
	if (stream.read(buf, PAGE_SIZE, head.lmt_position) == -1)
	{
		rc = -errno;
		S5LOG_ERROR("read block entry head failed rc:%d", rc);
	}
	int *buf_as_int = (int*)buf;

	int key_count = buf_as_int[0];

	LmtEntrySerializer reader(head.lmt_position + PAGE_SIZE, buf, buf_size, 0, &stream);
	{
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
		S5LOG_INFO("Load block map, key:%d ", key_count);
	}

	return 0;

}

/**
* delete an object, i.e. an allocation block
*/
int S5FlashStore::delete_obj(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba)
{
	//int64_t slba_aligned = (int64_t)CUT_LOW_10BIT(slba);
	//struct lmt_key key = { vol_id, slba_aligned };
	//struct lmt_entry* entry = (struct lmt_entry*)ht_remove(&obj_lmt, &key, sizeof(key));
	//if (entry == NULL)
	//	return 0;
	////offset = (obj_idx << OBJ_SIZE_ORDER) + store->meta_size
	//int obj_idx = (int)((entry->offset - meta_size) >> OBJ_SIZE_ORDER);
	//free(entry);
	//fsq_int_enqueue(&free_obj_queue, obj_idx);
	return 0;
}

int S5FlashStore::read_store_head()
{
	void* buf = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!buf)
	{
		S5LOG_ERROR("Failed to alloc memory in read_store_head");
		return -ENOMEM;
	}
	DeferCall([buf]() {
		free(buf);
	});
	if (-1 == tray->sync_read(buf, PAGE_SIZE, 0))
		return -errno;
	memcpy(&head, buf, sizeof(head));
	if (head.magic != 0x3553424e) //magic number, NBS5
		return -EUCLEAN;
	if(head.version != S5_VERSION) //S5 version
		return -EUCLEAN;
	return 0;
}

int S5FlashStore::process_event(int event_type, int arg_i, void* arg_p)
{
    S5LOG_FATAL("S5FlashStore::process_event not implemented");
    return 0;
}
