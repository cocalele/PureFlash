#include <unistd.h>
#include <errno.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <malloc.h>

#include "afs_flash_store.h"
#include "s5utils.h"
#include "afs_server.h"
#include "s5log.h"
#include "s5message.h"
#include "afs_cluster.h"


#define CUT_LOW_10BIT(x) (((unsigned long)(x)) & 0xfffffffffffffc00L)

/**
 * init flash store from device. this function will create meta data
 * and initialize the device if a device is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  device not exist or failed to open
 */
int flash_store::init(const char* mngt_ip, const char* dev_name)
{
	int ret = 0;
	safe_strcpy(dev_name, dev_name, sizeof(dev_name));
	store->dev_fd = open(dev_name, O_RDWR, O_DIRECT);
	if (store->dev_fd == -1)
		return -errno;

	if ((ret = read_store_head()) == 0)
	{
		ret = load_meta_data();
		if (ret)
			goto error1;
	}
	else if (ret == -EUCLEAN)
	{
		S5LOG_WARN("New device found, initializing  (%s) now ...", dev_name);
		if ((ret = initialize_store_head()) != 0)
		{
			S5LOG_ERROR("initialize_store_head failed ret(%d)", ret);
			goto error1;
		}
		int64_t obj_count = (dev_capacity - meta_size) >> OBJ_SIZE_ORDER;
		int qd = (int)((obj_count + 1023) & 0xfffffc00);
		ret = fsq_int_init(&free_obj_queue, qd);
		if (ret)
		{
			S5LOG_ERROR("free_obj_queue inititalize failed ret(%d)", ret);
			goto error1;
		}
		for (int i = 0; i < obj_count; i++)
		{
			fsq_int_enqueue(&free_obj_queue, i);
		}
		store->obj_lmt.reserve(obj_count * 2);
		save_meta_data();
	}
	else
		goto error1;
	return ret;
error2:
	fsq_int_destory(&free_obj_queue);
error1:
	close(store->dev_fd);
	store->dev_fd = 0;
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
int flash_store::read(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, /*out*/char* buf)
{
	int64_t slba_aligned = (int64_t)CUT_LOW_10BIT(slba);
	struct lmt_key key={vol_id, slba_aligned};
	size_t vs;
	auto it = obj_lmt.find(key);
	if(it == obj_lmt.end())
		return -ENOENT;

	int rc = (int)pread(dev_fd, buf, (size_t)nlba<<LBA_LENGTH_ORDER, it->second->offset + ((slba%1024) << LBA_LENGTH_ORDER));
	if(rc == -1)
		return -ENOMEM;
	return rc;
}

/**
 * write data to flash store.
 * a LBA is a block of data 4096 bytes.
 * @return number of lbas has written to store
 *         negative value for error
 */

int flash_store::write(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, char* buf)
{
	int64_t slba_aligned = (int64_t)CUT_LOW_10BIT(slba);
	struct lmt_key key={vol_id, slba_aligned};
	size_t vs;
	int64_t offset;
	auto it = obj_lmt.find(key);
	if (it == obj_lmt.end())
	{
		if (free_obj_queue.is_empty())
			return -ENOSPC;
		int obj_idx = free_obj_queue.dequeue();
		struct lmt_entry *new_entry = lmt_entry_pool.alloc();
		if (new_entry == NULL)
			throw std::logic_error("No lmt entry to alloc");
		*new_entry = { (obj_idx << OBJ_SIZE_ORDER) + meta_size, snap_seq };
		obj_lmt[key] = new_entry;
		offset = new_entry->offset + ((slba % 1024) << LBA_LENGTH_ORDER);
	}
	else
		offset = it->second->offset + ((slba % 1024) << LBA_LENGTH_ORDER);
	int rc = (int)pwrite(dev_fd, buf, (size_t)nlba<<LBA_LENGTH_ORDER, offset);
	if(rc == -1)
		return -ENOMEM;
	return 0;
}

int flash_store::initialize_store_head(struct flash_store* store)
{
	long numblocks;
	if (ioctl(store->dev_fd, BLKGETSIZE, &numblocks))
		return -errno;
	store->dev_capacity = numblocks << 9;//512 byte per block
	store->meta_size = META_RESERVE_SIZE;
	uuid_generate(store->uuid);
	char head[4096];
	memset(head, 0, sizeof(head));
	*((unsigned int*)head) = 0x3553424e; //magic number, NBS5
	memcpy(head + 4, store->uuid, sizeof(uuid_t));
	*((unsigned int*)(head+20)) = 0x00010000; //S5 version
	if(-1 == pwrite(store->dev_fd, head, sizeof(head), 0))
		return -errno;
	return 0;
}

int flash_store::save_meta_data(struct flash_store* store)
{
	char buf[PAGE_SIZE]; //should this buf alloc with posix_memalign ?
	memset(buf, 0, sizeof(buf));
	int* buf_as_int = (int*)buf;
	buf_as_int[0] = store->free_obj_queue.queue_depth;
	buf_as_int[1] = store->free_obj_queue.head;
	buf_as_int[2] = store->free_obj_queue.tail;
	if (-1 == pwrite(store->dev_fd, buf, sizeof(buf), 1 << PAGE_SIZE_ORDER))
		return -errno;
	if (-1 == pwrite(store->dev_fd, store->free_obj_queue.data, (size_t)store->free_obj_queue.queue_depth * sizeof(int), 2L << PAGE_SIZE_ORDER))
		return -errno;

	buf_as_int[0] = (int)store->obj_lmt.key_count;
	buf_as_int[1] = (int)sizeof(struct lmt_entry);
	buf_as_int[2] = 0;
	if (-1 == pwrite(store->dev_fd, buf, sizeof(buf), 2050L << PAGE_SIZE_ORDER))
		return -errno;

	struct lmt_entry *buf_as_lmt = (struct lmt_entry*)buf;
	int entry_per_page = PAGE_SIZE / sizeof(struct lmt_entry);
	struct hash_table_value_iterator *lmt_iterator = ht_create_value_iterator(&store->obj_lmt);
	int page_count = 0;
	int i = 0;
	struct hash_entry *entry;
	while ((entry = ht_next(lmt_iterator)) != NULL)
	{
		buf_as_lmt[i] = *(struct lmt_entry*)entry->value;
		i++;
		if (i == entry_per_page)
		{
			if (-1 == pwrite(store->dev_fd, buf, sizeof(buf), (2051 + page_count) << PAGE_SIZE_ORDER))
				return -errno;
			page_count++;
		}
	}
	if (i != 0)
	{
		if (-1 == pwrite(store->dev_fd, buf, sizeof(buf), (2051 + page_count) << PAGE_SIZE_ORDER))
			return -errno;
		page_count++;
	}
	return 0;
}

int flash_store::load_meta_data(struct flash_store* store)
{
	char buf[PAGE_SIZE]; //should this buf alloc with posix_memalign ?
	if (-1 == pread(store->dev_fd, buf, sizeof(buf), 1 << PAGE_SIZE_ORDER))
		return -errno;
	int* buf_as_int = (int*)buf;
	int ret = fsq_int_init(&store->free_obj_queue, buf_as_int[0]);
	if (ret)
	{
		S5LOG_ERROR("free_obj_queue inititalize failed ret(%d)", ret);
		return ret;
	}

	store->free_obj_queue.head = buf_as_int[1];
	store->free_obj_queue.tail = buf_as_int[2];
	if (-1 == pread(store->dev_fd, store->free_obj_queue.data, (size_t)store->free_obj_queue.queue_depth * sizeof(int), 2 << PAGE_SIZE_ORDER))
		goto error1;

	long obj_count = (store->dev_capacity - store->meta_size) >> OBJ_SIZE_ORDER;
	ret = ht_init(&store->obj_lmt, 0, 0.05, (int)obj_count * 2);
	if (ret)
	{
		S5LOG_ERROR("LMT table inititalize failed ret(%d)", ret);
		goto error1;
	}

	struct lmt_entry *buf_as_lmt = (struct lmt_entry*)buf;
	int entry_per_page = PAGE_SIZE / sizeof(struct lmt_entry);
	if (-1 == pread(store->dev_fd, buf, sizeof(buf), 2050 << PAGE_SIZE_ORDER))
		goto error2;
	int key_count = buf_as_int[0];
	for (int page = 0; key_count > 0 ;page++)
	{
		if (-1 == pread(store->dev_fd, buf, sizeof(buf), (2051 + page) << PAGE_SIZE_ORDER))
			goto error2;
		for (int i = 0; i < entry_per_page && key_count > 0; i++)
		{
			ht_insert(&store->obj_lmt, &buf_as_lmt[i].key, sizeof(struct lmt_key), &buf_as_lmt[i], sizeof(struct lmt_entry));
		}
	}
	return 0;
error2:
	ret = -errno;
	ht_destroy(&store->obj_lmt);
error1:
	if(!ret)
		ret = -errno;
	fsq_int_destory(&store->free_obj_queue);
	return ret;
}

/**
* delete an object, i.e. an allocation block
*/
int flash_store::delete_obj(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba)
{
	int64_t slba_aligned = (int64_t)CUT_LOW_10BIT(slba);
	struct lmt_key key = { vol_id, slba_aligned };
	struct lmt_entry* entry = (struct lmt_entry*)ht_remove(&obj_lmt, &key, sizeof(key));
	if (entry == NULL)
		return 0;
	//offset = (obj_idx << OBJ_SIZE_ORDER) + store->meta_size
	int obj_idx = (int)((entry->offset - store->meta_size) >> OBJ_SIZE_ORDER);
	free(entry);
	fsq_int_enqueue(&free_obj_queue, obj_idx);
	return 0;
}

int flash_store::read_store_head()
{
	//char head[4096];
	//memset(head, 0, sizeof(head));
	char *head = memalign(PAGE_SIZE, PAGE_SIZE);// posix_memalign()
	if (-1 == pread(dev_fd, head, PAGE_SIZE, 0))
		return -errno;
	long numblocks;
	if (ioctl(dev_fd, BLKGETSIZE, &numblocks))
		return -errno;
	dev_capacity = numblocks << 9;//512 byte per block
	meta_size = META_RESERVE_SIZE;
	if (*((unsigned int*)head) != 0x3553424e) //magic number, NBS5
		return -EUCLEAN;
	if(*((unsigned int*)(head + 20)) != 0x00010000) //S5 version
		return -EUCLEAN;
	memcpy(uuid, head + 4, sizeof(uuid_t));
	return 0;
}