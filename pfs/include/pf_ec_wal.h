struct PfEcWalEntry
{
	int64_t vol_off;
	int64_t aof_off;
	int aof_new_section_index;
	int aof_old_section_index;
	int64_t aof_section_length; //length of aof_new_section
	int64_t aof_section_garbage_length; //garbage length of aof_old_section
	int64_t _pad1;
	int64_t _pad2;
	int64_t _pad3;
};
static_assert(sizeof(struct pf_ec_wal_entry) == 64, "pf_ec_wal_entry unexcepted size");

struct PfEcWalHead
{
	
};
struct PfEcWalPage
{

};


/**
 * Layout of EC redolog:
 * Part      |   length(Byte)  |
 * ----------+-----------------+--------------------------------
 * part1     |  4096           |  head page,
 * part2     |  1G             | redo log area1
 * part3     |  1G             | redo log area2
 * Next part not belongs redo log, but index memory page
 * part4     |  256G           | forward lut page
 * part5     |  256G           | reverse lut page
 * 
 */
struct PfEcWal{
public:
	PfEcWalEntry* alloc_entry();
};