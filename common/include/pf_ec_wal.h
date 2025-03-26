#ifndef pf_ec_wal_h__
#define pf_ec_wal_h__

#include "pf_client_priv.h"

class PfEcClientVolume;
struct PfEcRedologEntry
{
	int64_t vol_off;
	int64_t aof_off; //from this to deduce new section index , since each section length is 16GB
	//int16_t aof_new_section_index; //section this write to

	uint8_t old_section_index[64]; //at most 64 section, since maximum IO limit 256KB, i.e. 64 LBA
};
static_assert(sizeof(struct PfEcRedologEntry) == 80, "pf_ec_wal_entry unexpected size");

//struct alignas(4096) struct PfEcVolumeHead
//{
//	int64_t phase;
//	size_t size;
//	off_t start_offset;
//	off_t current_offset;
//
//};

#define ENTRY_PER_PAGE 50

struct alignas(4096) PfEcRedologPage
{
	int64_t phase;
	uint32_t free_index; //index of free entry to use
	uint32_t _pad1;
	PfList<PfClientIocb, &PfClientIocb::ec_next> waiting_io;
	int64_t offset;//offset in redolog volume
	PfEcRedologPage* next;
	PfEcClientVolume* owner_volume;
	int64_t _pad[5];

	PfEcRedologEntry entries[ENTRY_PER_PAGE];
	PfEcRedologPage(){}
	int init(int phase, off_t offset);

	static const int64_t PageSize = 4096;
};
static_assert(sizeof(struct PfEcRedologPage) == 4096);
enum PfRedologPageState {
	LP_FREE = 0,
	LP_FLUSHING = 1,
	LP_INUSE = 2,
};
#define PAGE_OF_WAL(w) ((struct PfEcRedologPage*)(w&(~0x0fffULL)))

/**
 * Layout of EC redolog:
 * Part      |   length(Byte)  |
 * ----------+-----------------+--------------------------------
 * part1     |  4096           | redo log head page,
 * part2     |  1G             | redo log area1
 * part3     |  1G             | redo log area2
 * Next part not belongs redo log, but index memory page
 * part4     |  256G           | forward lut page
 * part5     |  256G           | reverse lut page
 * 
 */
struct PfEcRedolog{
public:
	struct Zone {
		bool need_flush;
		int inflying_commit_io;
		int64_t phase;
		off_t start_offset;
		off_t end_offset;
		off_t current_offset;
	} zone[2];
	//size_t size;

	Zone* current_zone;

	PfEcClientVolume *owner_volume;  //which volume this redolog belongs to
	PfReplicatedVolume *meta_volume; //where this redolog persisted in

	PfEcRedologPage pages[2]; //ping-pong page
	PfEcRedologPage* current_page;

	ObjectMemoryPool<PfEcRedologEntry> entry_pool;

	PfList< PfEcRedologPage, &PfEcRedologPage::next> free_pages; //free page to use
	PfList< PfEcRedologPage, &PfEcRedologPage::next> commit_pages; //pages  waiting to be persist to disk


	int init(PfEcClientVolume* owner, PfReplicatedVolume* meta);
	PfEcRedologEntry* alloc_entry();
	void commit_entry(PfEcRedologEntry* e, PfClientIocb* io);
	int set_log_phase(int64_t _phase, uint64_t offset);
private:
	//inline int current_zone_index() { return start_offset == owner_volume->head.redolog_position_first ? 0 : 1; }
	//inline off_t opposite_offset() {
	//	return start_offset == owner_volume->head.redolog_position_first ? owner_volume->head.redolog_position_second : owner_volume->head.redolog_position_first;}
};
#endif // pf_ec_wal_h__
