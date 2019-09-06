#ifndef flash_store_h__
#define flash_store_h__
#include <uuid/uuid.h>
#include <unordered_map>
#include <stdint.h>
#include "fixed_size_queue.h"
#include "s5conf.h"

#define META_RESERVE_SIZE (128L*1024*1024)
#define OBJ_SIZE (4L*1024*1024)  //4M byte per object
#define OBJ_SIZE_ORDER 22 //4M = 2 ^ 22
#define PAGE_SIZE 4096
#define PAGE_SIZE_ORDER 12

struct toedaemon;

/**
 * key of 4M block
 */
struct lmt_key
{
	uint64_t vol_id;
	int64_t slba; //align on 4M block
};
/**
 * represent a 4M block entry
 */
struct lmt_entry
{
	int64_t offset; //offset of this 4M block in device. in bytes
	uint32_t snap_seq;
};
inline bool operator == (const lmt_key &k1, const lmt_key &k2) { return k1.vol_id == k2.vol_id && k1.slba == k2.slba; }

	struct lmt_hash
	{
		std::size_t operator()(const struct lmt_key& k) const
		{
			using std::size_t;
			using std::hash;
			using std::string;

			// Compute individual hash values for first,
			// second and third and combine them using XOR
			// and bit shifting:
			const size_t _FNV_offset_basis = 14695981039346656037ULL;
			const size_t _FNV_prime = 1099511628211ULL;
			return (((k.vol_id << 8)*k.slba) ^ _FNV_offset_basis)*_FNV_prime;

		}
	};


class flash_store
{
public:
	char dev_name[256];
	uuid_t  uuid;
	int64_t dev_capacity; //total capacity of device, in byte,
	int64_t meta_size;//reserved size for meta data, remaining for user store, general 50G byte

	std::unordered_map<struct lmt_key, struct lmt_entry*, struct lmt_hash> obj_lmt; //act as lmt table in S5
	fixed_size_queue<int> free_obj_queue;
	ObjectMemoryPool<lmt_entry> lmt_entry_pool;

	int dev_fd;


/**
 * init flash store from device. this function will create meta data
 * and initialize the device if a device is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  device not exist or failed to open
 */
int init(const char* mngt_ip, const char* dev_name);

/**
 * read data to buffer.
 * a LBA is a block of data 4096 bytes.
 * @return actual data length that readed to buf, in lba.
 *         negative value for error
 * actual data length may less than nlba in request to read. in this case, caller
 * should treat the remaining part of buffer as 0.
 */
int read(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, /*out*/char* buf);

/**
 * write data to flash store.
 * a LBA is a block of data 4096 bytes.
 * @return number of lbas has written to store
 *         negative value for error
 */
int write(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba, char* buf);

/**
 * delete a 4M object
 */
int delete_obj(uint64_t vol_id, int64_t slba,
	int32_t snap_seq, int32_t nlba);
private:
	int read_store_head();
	int initialize_store_head();
	int save_meta_data();
	int load_meta_data();
};

#endif // flash_store_h__
