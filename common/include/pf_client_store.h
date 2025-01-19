#include <uuid/uuid.h>
#include <unordered_map>
#include <stdint.h>
#include <libaio.h>
#include <thread>
#include <future>

#include "pf_mempool.h"
#include "pf_lmt.h"
#include "pf_ioengine.h"

class PfReplicatedVolume;
/**
 * This class is equivalent to PfFlashStore in server side.
 * This class is used to access disk directly from client (bypass store server)
 */ 
class PfClientStore
{
public:
	//HeadPage must exactly same as definition as that in store side
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
		uint64_t free_list_position_first;
		uint64_t free_list_position_second;
		uint64_t free_list_size_first;
		uint64_t free_list_size_second;
		uint64_t trim_list_position_first;
		uint64_t trim_list_position_second;
		uint64_t trim_list_size;
		uint64_t lmt_position_first;
		uint64_t lmt_position_second;
		uint64_t lmt_size;
		uint64_t redolog_position_first;
		uint64_t redolog_position_second;
		uint64_t redolog_size;
		/**update after save metadata**/
		int64_t  redolog_phase;
		uint8_t  current_metadata;
		uint8_t  current_redolog;
		char md5_first[MD5_RESULT_LEN];
		char md5_second[MD5_RESULT_LEN];
		/***/
		char create_time[32];
	};

	//following are hot variables used by every IO. Put compact for cache hit convenience
	union {
		int fd;
		//struct ns_entry* ns;
	};
	uint64_t in_obj_offset_mask; // := obj_size -1,
	PfReplicatedVolume* volume;
	std::unordered_map<struct lmt_key, struct lmt_entry*, struct lmt_hash> obj_lmt; //act as lmt table in S5

	ObjectMemoryPool<lmt_entry> lmt_entry_pool;
	PfIoEngine* ioengine;
	HeadPage head;
	char tray_name[128];
	char uuid_str[64];

	int is_shared_disk;
	void zk_watch_proc();
	std::thread zk_watch_thread;

	~PfClientStore();
	/**
	 * init flash store from device. this function will create meta data
	 * and initialize the device if a device is not initialized.
	 *
	 * @return 0 on success, negative for error
	 * @retval -ENOENT  device not exist or failed to open
	 */
	int init(PfReplicatedVolume* vol, const char* dev_name, const char* dev_uuid);


	/**
	 * read data to buffer.
	 * a LBA is a block of data 4096 bytes.
	 * @return actual data length that readed to buf, in lba.
	 *         negative value for error
	 * actual data length may less than nlba in request to read. in this case, caller
	 * should treat the remaining part of buffer as 0.
	 */
	int do_read(IoSubTask* io);

	/**
	 * write data to flash store.
	 * a LBA is a block of data 4096 bytes.
	 * @return number of lbas has written to store
	 *         negative value for error
	 */
	int do_write(IoSubTask* io);

	inline void begin_cow(lmt_key* key, lmt_entry* srcEntry, lmt_entry* dstEntry)
	{
		auto f = std::async(std::launch::async, [this, key, srcEntry, dstEntry] {do_cow_entry(key, srcEntry, dstEntry); });
	}
	int delete_obj(struct lmt_key* key, struct lmt_entry* entry);

	
private:
	void do_cow_entry(lmt_key* key, lmt_entry* srcEntry, lmt_entry* dstEntry);

	int read_store_head();

	/** these two function convert physical object id (i.e. object in disk space) and offset in disk
	 *  Note: don't confuse these function with vol_offset_to_block_idx, which do convert
	 *        in volume space
	 */
	inline int64_t obj_id_to_offset(int64_t obj_id) { return (obj_id << head.objsize_order) + head.meta_size; }
	inline int64_t offset_to_obj_id(int64_t offset) { return (offset - head.meta_size) >> head.objsize_order; }

};
