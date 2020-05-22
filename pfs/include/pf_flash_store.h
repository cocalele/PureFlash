#ifndef flash_store_h__
#define flash_store_h__
#include <uuid/uuid.h>
#include <unordered_map>
#include <stdint.h>
#include "pf_fixed_size_queue.h"
#include "basetype.h"
#include "pf_conf.h"
#include "pf_mempool.h"
#include "pf_event_queue.h"
#include "pf_event_thread.h"
#include "pf_tray.h"


#define META_RESERVE_SIZE (40LL<<30) //40GB

#define OBJ_SIZE_ORDER 24
#define OBJ_SIZE (1<<OBJ_SIZE_ORDER)
#define S5_VERSION 0x00020000

class PfRedoLog;
class IoSubTask;
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
static_assert(sizeof(lmt_key) == 32, "unexpected lmt_key size");
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
	IoSubTask* waiting_io;
};
static_assert(sizeof(lmt_entry) == 32, "unexpected lmt_entry size");

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

class PfFlashStore : public PfEventThread
{
public:
	struct HeadPage {
		uint32_t magic;
		uint32_t version;
		unsigned char uuid[16];
		uint32_t key_size;
		uint32_t entry_size;
		uint64_t objsize;
		uint64_t tray_capacity;
		uint64_t meta_size;
		uint32_t objsize_order; //objsize = 2 ^ objsize_order
		uint32_t rsv1; //to make alignment at 8 byte
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

	//following are hot variables used by every IO. Put compact for cache hit convenience
	int fd;
	uint64_t in_obj_offset_mask; // := obj_size -1,
	io_context_t aio_ctx;
	pthread_t polling_tid; //polling thread

	std::unordered_map<struct lmt_key, struct lmt_entry*, struct lmt_hash> obj_lmt; //act as lmt table in S5
	PfFixedSizeQueue<int32_t> free_obj_queue;
	PfFixedSizeQueue<int32_t> trim_obj_queue;
	ObjectMemoryPool<lmt_entry> lmt_entry_pool;
	PfRedoLog* redolog;
	char tray_name[256];
	HeadPage head;


	/**
	 * init flash store from device. this function will create meta data
	 * and initialize the device if a device is not initialized.
	 *
	 * @return 0 on success, negative for error
	 * @retval -ENOENT  device not exist or failed to open
	 */
	int init(const char* dev_name);

	int process_event(int event_type, int arg_i, void* arg_p);
	int preocess_io_event(IoSubTask* io);

	void aio_polling_proc();
	std::thread aio_poller;

	/**
	 * read data to buffer.
	 * a LBA is a block of data 4096 bytes.
	 * @return actual data length that readed to buf, in lba.
	 *         negative value for error
	 * actual data length may less than nlba in request to read. in this case, caller
	 * should treat the remaining part of buffer as 0.
	 */
	inline int do_read(IoSubTask* io);

	/**
	 * write data to flash store.
	 * a LBA is a block of data 4096 bytes.
	 * @return number of lbas has written to store
	 *         negative value for error
	 */
	int do_write(IoSubTask* io);

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
	inline int64_t obj_id_to_offset(int64_t obj_id) { return (obj_id << head.objsize_order) + head.meta_size; }
};

#endif // flash_store_h__
