#ifndef flash_store_h__
#define flash_store_h__
#include <uuid/uuid.h>
#include <unordered_map>
#include <stdint.h>
#include "s5_fixed_size_queue.h"
#include "basetype.h"
#include "s5conf.h"
#include "s5_mempool.h"
#include "s5_event_queue.h"
#include "s5_event_thread.h"


#define META_RESERVE_SIZE (40LL<<30) //40GBsetype.h"

#define OBJ_SIZE_ORDER 24
#define OBJ_SIZE (1<<OBJ_SIZE_ORDER)
class S5RedoLog;

/**
 * key of 4M block
 */
struct lmt_key
{
	uint64_t vol_id;
	int64_t slba; //align on 4M block
	int64_t rsv1;
	int64_t rsv2;
};
static_assert(sizeof(lmt_key) == 32, "lmt_key");
enum EntryStatus: uint32_t {
	UNINIT = 0, //not initialized
	NORMAL = 1,
	COPYING = 2, //COW on going
};
/**
 * represent a 4M block entry
 */
struct lmt_entry
{
	int64_t offset; //offset of this 4M block in device. in bytes
	uint32_t snap_seq;
	uint32_t status; // type EntryStatus
	lmt_entry* prev_snap;
	void* waiting_io;
};
static_assert(sizeof(lmt_entry) == 32, "lmt_entry");

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

#ifdef USE_SPDK
struct ns_entry* dev_handle_t;
#else
typedef int dev_handle_t;
#endif

class S5FlashStore : public S5EventThread
{
public:
	struct HeadPage {
		uint32_t magic;
		uint32_t version;
		unsigned char uuid[16];
		uint32_t key_size;
		uint32_t entry_size;
		uint64_t objsize;
		uint32_t objsize_order; //objsize = 2 ^ objsize_order
		uint32_t rsv1; //to make alignment at 8 byte
		uint64_t dev_capacity;
		uint64_t meta_size;
		uint64_t free_list_position;
		uint64_t free_list_size;
		uint64_t trim_list_position;
		uint64_t trim_list_size;
		uint64_t lmt_position;
		uint64_t lmt_size;
		uint64_t metadata_md5_position;
		uint64_t head_backup_position;
		uint64_t redolog_position;
		uint64_t redolog_size;
		char create_time[32];
	};
	char dev_name[256];
	HeadPage head;

	dev_handle_t dev_fd;
	std::unordered_map<struct lmt_key, struct lmt_entry*, struct lmt_hash> obj_lmt; //act as lmt table in S5
	S5FixedSizeQueue<int32_t> free_obj_queue;
	S5FixedSizeQueue<int32_t> trim_obj_queue;
	ObjectMemoryPool<lmt_entry> lmt_entry_pool;
	S5RedoLog* redolog;

/**
 * init flash store from device. this function will create meta data
 * and initialize the device if a device is not initialized.
 *
 * @return 0 on success, negative for error
 * @retval -ENOENT  device not exist or failed to open
 */
int init(const char* dev_name);

int process_event(int event_type, int arg_i, void* arg_p);

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

	int save_meta_data();
private:
	int read_store_head();
	int initialize_store_head();
	int load_meta_data();
};

#endif // flash_store_h__
