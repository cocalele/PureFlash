#ifndef flash_store_h__
#define flash_store_h__
#include <uuid/uuid.h>
#include "hashtable.h"
#include "fixed_size_queue.h"
#include "s5conf.h"

#define META_RESERVE_SIZE (128L*1024*1024)
#define OBJ_SIZE (4L*1024*1024)  //4M byte per object
#define OBJ_SIZE_ORDER 22 //4M = 2 ^ 22
#define PAGE_SIZE 4096
#define PAGE_SIZE_ORDER 12

struct toedaemon;

struct flash_store
{
	char dev_name[256];
	uuid_t  uuid;
	int64_t dev_capacity; //total capacity of device, in byte,
	int64_t meta_size;//reserved size for meta data, remaining for user store, general 1G byte

	struct hash_table obj_lmt; //act as lmt table in S5
	struct fixed_size_queue_int free_obj_queue;

	int dev_fd;
};

/**
 * key of 4M block
 */
struct lmt_key
{
	uint64_t vol_id;
	int64_t slba; //align on 4M block
	int64_t snap_seq;
};

/**
 * represent a 4M block entry
 */
struct lmt_entry
{
	struct lmt_key key;
	int64_t offset; //offset of this 4M block in device. in bytes
};
/**
 * init flash store from device. this function will create meta data
 * and initialize the device if a device is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  device not exist or failed to open
 */
int fs_init(const char* mngt_ip, struct flash_store* store, const char* dev_name);

/**
 * read data to buffer. 
 * a LBA is a block of data 4096 bytes.
 * @return actual data length that readed to buf, in lba. 
 *         negative value for error
 * actual data length may less than nlba in request to read. in this case, caller
 * should treat the remaining part of buffer as 0.
 */
int fs_read(struct flash_store* store, uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, /*out*/char* buf);

/**
 * write data to flash store.
 * a LBA is a block of data 4096 bytes.
 * @return number of lbas has written to store
 *         negative value for error
 */
int fs_write(struct flash_store* store, uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, char* buf);

/**
 * delete a 4M node
 */
int fs_delete_node(struct flash_store* store, uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba);

#endif // flash_store_h__
