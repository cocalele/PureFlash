#ifndef pf_lmt_h__
#define pf_lmt_h__

#include <stdint.h>

class IoSubTask;
class IoSubTask;
class PfBitmap;
/**
 * key of 4M block
 */
struct lmt_key
{
	uint64_t vol_id;
	int64_t slba; //a lba is 4K. slba should align on block
	int64_t rsv1;
	int64_t rsv2;
	std::string to_string() const{
		return format_string("<0x%lx-%ld>", vol_id, slba);
	}
};
static_assert(sizeof(lmt_key) == 32, "unexpected lmt_key size");
enum EntryStatus : uint32_t {
	UNINIT = 0, //not initialized
	NORMAL = 1,
	COPYING = 2, //COW on going
	DELAY_DELETE_AFTER_COW = 3,
	RECOVERYING = 4,
};
/**
 * represent a 4M block entry
 */
struct lmt_entry
{
	int64_t offset; //offset of this block in device. in bytes
	uint32_t snap_seq;
	EntryStatus status; // type EntryStatus
	lmt_entry* prev_snap;

	IoSubTask* waiting_io;

	PfBitmap* recovery_bmp;
	void* recovery_buf;

	void init_for_redo() {
		//all other variable got value from redo log
		prev_snap = NULL;
		waiting_io = NULL;
		recovery_bmp = NULL;
		recovery_buf = NULL;
	}
};

static_assert(sizeof(lmt_entry) == 48, "unexpected lmt_entry size");

inline bool operator == (const lmt_key& k1, const lmt_key& k2) { return k1.vol_id == k2.vol_id && k1.slba == k2.slba; }

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
		return (((k.vol_id << 8) * k.slba) ^ _FNV_offset_basis) * _FNV_prime;

	}
};
static inline int insert_lmt_entry_list(struct lmt_entry** head_ref, lmt_entry* entry, 
	std::function<bool(struct lmt_entry*)> after_entry, std::function<bool(struct lmt_entry*)> same_entry)
{
	struct lmt_entry* head = *head_ref;
	if(same_entry(head)){
		return -EEXIST;
	}
	if (!after_entry(head)) { //insert as head
		entry->prev_snap = head;
		*head_ref = entry; //insert as head
		return 0;
	}
	while(head != NULL){
		if(same_entry(head))
			return -EEXIST;
		if(head->prev_snap == NULL){
			head->prev_snap = entry; //insert as last
			return 0;
		} else if(after_entry(head->prev_snap)){
			head = head->prev_snap;
		} else {
			entry->prev_snap = head->prev_snap;
			head->prev_snap = entry; //insert in middle
			return 0;
		}
	}
	assert(0);
	return -ENOENT; //never reach here
	
}
static inline void delete_matched_entry(struct lmt_entry** head_ref, std::function<bool(struct lmt_entry*)> match,
	std::function<void(struct lmt_entry*)> free_func)
{
	// Store head node
	struct lmt_entry* temp = *head_ref, * prev;

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
#endif // pf_lmt_h__
