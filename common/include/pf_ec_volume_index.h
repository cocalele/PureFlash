#ifndef pf_ec_volume_index_h__
#define pf_ec_volume_index_h__

#include "pf_list.h"
#include "pf_client_priv.h"

#define PF_EC_INDEX_PAGE_SIZE (64<<10)
#define PF_OFF2PTE_INDEX(x) ((x)>>16) //i.e. x/64K
#define PF_INDEX_CNT_PER_PAGE (8<<10) //i.e. 8K
static_assert(PF_EC_INDEX_PAGE_SIZE/sizeof(int64_t) == PF_INDEX_CNT_PER_PAGE);
class PfEcClientVolume;
class PfEcVolumeIndex;


enum PteState : uint8_t {
	PAGE_UNPRESENT = 0,
	PAGE_PRESENT = 1, //also means PAGE_CLEAN
	PAGE_LOADING = 2,
	PAGE_DIRTY = 3,
	PAGE_FLUSHING = 4,
};

class PfLutPage {
public:
	union {
		void* addr; //address of page
		int64_t* lut;
	};
	int lru_count;
	volatile PteState state;
	PfList<PfClientIocb, &PfClientIocb::ec_next> waiting_list;
	struct PfLutPte* pte;
	PfEcClientVolume* owner;
	PfLutPage* next; //linked in either of free_list, dirty_list, clean_list
	inline PfEcClientVolume* owner_volume() { return owner; }
	//int64_t offset(); //offset of this page in meta area, plus fwd_lut_offset or rvs_lut_offset to get real offset on disk
	uint32_t pfn(PfEcVolumeIndex* owner);
};


#pragma  pack(1)
struct PfLutPte{
union{
	uint64_t val;
	struct{
		uint8_t _page[4];
		uint32_t pfn;
	};
	};
	PfLutPage* page(PfEcVolumeIndex* owner); //get page accord pfn
	

	int64_t offset(PfEcVolumeIndex* owner);//offset of this page in meta area, plus fwd_lut_offset or rvs_lut_offset to get real offset on disk

};
#pragma  pack()
static_assert(sizeof(PfLutPte) == 8, "Unexpected size");




////IO processing state, IO is processed in asynchronous mode,
enum EcIoState : unsigned int
{
	APPENDING_AOF = 0,//appending data to aof
	//FILLING_WAL = 1,
	UPDATING_FWD_LUT = 2, //updating forward lut table
	UPDATING_RVS_LUT = 3, //updating reverse lut table
	COMMITING_WAL = 4,
};
class PfRoutine;
class PfEcVolumeIndex
{
public:
	PfEcVolumeIndex(PfEcClientVolume* owner, int  page_count);
	//set offset of lba in aof, 调用set函数前要保证需要的Lut 页加载在内存中
	void set_forward_lut(int64_t vol_offset, int64_t aof_offset);
	void set_reverse_lut(int64_t vol_offset, int64_t aof_offset);

	//带co_前缀的表示是协程模式，这中模式的set函数内部会自己处理缺页
	void co_sync_set_forward_lut(int64_t vol_offset, int64_t aof_offset);
	void co_sync_set_reverse_lut(int64_t vol_offset, int64_t aof_offset);

	//lookup forward lut to get aof_offset.
	int64_t get_forward_lut(int64_t vol_off);
	int64_t get_reverse_lut(int64_t aof_offset);

	int co_flush_once(/*bool initial*/);

	PfLutPte* get_fwd_pte(int64_t pte_index);
	PfLutPte* get_rvs_pte(int64_t pte_index);

	//flush internal state control, think as local variable of a coroutine
	//struct FlushCb{
	//	int inflying_io;
	//	int page_idx;
	//	int rc;
	//}flush_cb;
	//end internal state
	PfEcClientVolume* owner;
	PfReplicatedVolume* meta_volume;

	int load_page(PfClientIocb* client_io, PfLutPage* page);
	int co_sync_load_page(PfLutPage* page);

	//allocate a free page
	PfLutPage* get_page();
	PfList<PfLutPage, &PfLutPage::next> free_pages;
	PfList<PfLutPage, &PfLutPage::next> dirty_pages;
	PfList<PfLutPage, &PfLutPage::next> clean_pages;
	int page_cnt; //count of pages
	PfLutPte *fwd_pte;
	PfLutPte *rvs_pte;

	PfLutPage pages[0];
	
};


#endif // pf_ec_volume_index_h__
