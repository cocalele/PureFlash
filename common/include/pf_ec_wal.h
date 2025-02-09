#ifndef pf_ec_wal_h__
#define pf_ec_wal_h__

#include "pf_client_priv.h"

struct PfEcRedologEntry
{
	int64_t vol_off;
	int64_t aof_off; //from this to deduce new section index , since each section length is 16GB
	//int16_t aof_new_section_index; //section this write to

	int8_t old_section_index[64]; //at most 64 section, since maximum IO limit 256KB, i.e. 64 LBA
};
static_assert(sizeof(struct pf_ec_wal_entry) == 48, "pf_ec_wal_entry unexpepted size");

struct PfEcWalHead
{
	
};

#define ENTRY_PER_PAGE 84

struct alignas(4096) PfEcRedologPage
{
	uint32_t phase;
	uint32_t free_index; //index of free entry to use
	PfList<PfClientIocb> waiting_io;
	int64_t _pad[6];

	PfEcRedologEntry entries[ENTRY_PER_PAGE];
	PfEcRedologPage():waiting_io(&PfClientiocb::ec_next){}
};
static_assert(sizeof(struct PfEcRedologPage) == 4096);
enum PfRedologPageState {
	LP_FREE = 0;
	LP_FLUSHING = 1;
	LP_INUSE = 2;
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
	PfEcRedologEntry* alloc_entry();
	void commit_entry(PfEcRedologEntry* e);
	PfEcRedologPage page[2]; //ping-pong page
	PfEcRedologPage* current_page;

	PfList< PfEcRedologPage> free_pages; //free page to use
	PfList< PfEcRedologPage> commit_pages; //pages  waiting to be persist to disk
};
#endif // pf_ec_wal_h__
