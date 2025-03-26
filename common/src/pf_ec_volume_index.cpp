#include <stdlib.h>
#include "pf_ec_client_volume.h"
#include "pf_ec_volume_index.h"
#include "pf_coroutine.h"

PfEcVolumeIndex::PfEcVolumeIndex(PfEcClientVolume* owner)
{
	this->owner = owner;
	this->meta_volume = owner->meta_volume;

	size_t sz = owner->volume_size;
	size_t aof_sz = (size_t)(sz*1.2); //suppose 20% over provision on aof
	int64_t pte_cnt = ((sz/LBA_LENGTH)*8)/ PF_EC_INDEX_PAGE_SIZE;
	int64_t rvs_pte_cnt = ((aof_sz / LBA_LENGTH) * 8) / PF_EC_INDEX_PAGE_SIZE;

	Cleaner _c;

	fwd_pte = new PfLutPte[pte_cnt]; //will throw bad_alloc on fail
	if(fwd_pte == NULL){
		S5LOG_FATAL("Failed to alloc fwd_pte"); //never reach here
		
	}
	_c.push_back([this](){delete[] fwd_pte;});
	rvs_pte = new PfLutPte[rvs_pte_cnt];
	_c.push_back([this]() {delete[] rvs_pte; });
	if (rvs_pte == NULL) {
		S5LOG_FATAL("Failed to alloc rvs_pte"); //never reach here

	}
	_c.cancel_all();
}
int do_io_write(PfClientIocb* iocb, PfEcClientVolume* vol);// do io 
static void page_load_cbk(void* cbk_arg, int complete_status)
{

	if(complete_status != PfMessageStatus::MSG_STATUS_SUCCESS){
		S5LOG_ERROR("page load failed, rc:%d", complete_status);
	}
	
	PfLutPage* page = (PfLutPage*)cbk_arg;
	PfEcClientVolume* vol = page->owner;
	for (PfClientIocb* p = page->waiting_list.head; p != NULL; p = p->ec_next) {
		do_io_write(p,vol);
	}
	page->waiting_list.clear();
}
//1. 实现coroutine
//2. 像android上实现模态对话框一样，进行内部的事件循环，外部逻辑连续
//3. .net await模式，或者c++ promise模式。 这个模式需要外部线程池，不行
//4. EC context, 状态机. 我们使用这一种，
int PfEcVolumeIndex::load_page(PfClientIocb* client_io, PfLutPage* page)
{
	page->owner = owner;
	assert(page->state != PAGE_PRESENT);
	page->waiting_list.push_back(client_io);
	int rc = 0;
	if (page->state == PAGE_LOADING )
	{
		return 0;
	}
	rc = meta_volume->io_submit(page->addr, PF_EC_INDEX_PAGE_SIZE, page->pte->offset(this), page_load_cbk, page, false);
	if(rc){
		S5LOG_ERROR("Failed to submit read IO, volume:%s offset:%ld", meta_volume->volume_name.c_str(), page->pte->offset(this));
		return rc;
	}
	return 0;
}
int PfEcVolumeIndex::set_forward_lut( int64_t vol_offset, int64_t aof_offset)
{
	PfLutPte *pte = &fwd_pte[PF_OFF2PTE_INDEX(vol_offset)];
	PfLutPage* page = pte->page(this);

	int index = (int)((vol_offset >> LBA_LENGTH_ORDER) % PF_INDEX_CNT_PER_PAGE);
	page->lut[index]=aof_offset; //update data in memory
	page->state=PAGE_DIRTY;//mark page dirty
}
int PfEcVolumeIndex::set_reverse_lut(int64_t vol_offset, int64_t aof_offset)
{

}

int64_t PfEcVolumeIndex::get_forward_lut(int64_t vol_off)
{

}
PfLutPte* PfEcVolumeIndex::get_fwd_pte(int64_t pte_index)
{
	return &fwd_pte[pte_index];
}
PfLutPte* PfEcVolumeIndex::get_rvs_pte(int64_t pte_index)
{
	return &rvs_pte[pte_index];
}
PfLutPage* PfEcVolumeIndex::get_page()
{
	//找到一个空闲页面
	S5LOG_ERROR("Not implemented");
	return NULL;
}
//static int lut_flush_cbk(void* cbk_arg, int complete_status)
//{
//	PfLutPage* page = (PfLutPage *) cbk_arg;
//	if(page->state == PAGE_FLUSHING){ //page may have become dirty during flush, not change to PRESENT on this case
//		page->state = PAGE_PRESENT;
//	} else {
//		S5LOG_DEBUG("page state changed during flush");
//	}
//	PfEcVolumeIndex* ec_index = page->owner->ec_index;
//	ec_index->flush_cb.inflying_io --;
//	if(complete_status){
//		ec_index->flush_cb.rc = complete_status;
//	} else {
//		ec_index->flush(false);//continue to flush
//	}
//
//	if((ec_index->flush_cb.page_idx == ec_index->page_cnt || ec_index->flush_cb.rc != 0) && ec_index->flush_cb.inflying_io == 0) {
//		page->owner->event_queue->post_event(EVT_FLUSH_META_COMPLETE, ec_index->flush_cb.rc, NULL, page->owner);
//	}
//	return 0;
//}



#define FLUSH_IO_DEPTH 8
int PfEcVolumeIndex::co_flush_once(/*bool initial */)
{
	//1. index memory to disk
	//2. discard redo log && persist head page
#if 1 //use coroutine
	
	
	volatile int inflying_io = 0;
	volatile int io_rc = 0;
	for (int page_idx = 0; page_idx < page_cnt && io_rc == 0; page_idx++) {
		while(inflying_io >= FLUSH_IO_DEPTH){
			co_yield();
		}
		PfLutPage* page = &pages[page_idx];
		if (page->state == PAGE_DIRTY) {
			page->state = PAGE_FLUSHING;
			inflying_io++;
			off_t offset;
			
			offset = (page->pte > rvs_pte ? owner->head.rvs_lut_offset :  owner->head.fwd_lut_offset) + page->pte->offset(this);
			meta_volume->io_submit(page->addr, (size_t)PF_EC_INDEX_PAGE_SIZE, offset, (int)TRUE,
			[this, &inflying_io, &io_rc, offset](int complete_status)->void{
				inflying_io--;
				if(complete_status){
					S5LOG_ERROR("Failed flush lut, offset:0x%lx, rc:%d", offset, complete_status);
					io_rc = complete_status;
				}
				co_enter(owner->flush_routine);
			});
		}
	}
	while (inflying_io) {
		co_yield(); //wait all IO complete
	}

	if(io_rc){
		S5LOG_ERROR("Error during flush meta, rc:%d", io_rc);
	}

#else
	if(initial){
		flush_cb.page_idx = 0;
		flush_cb.inflying_io = 0;
	}
	
	for( ;flush_cb.inflying_io < FLUSH_IO_DEPTH; flush_cb.page_idx++){
		PfLutPage* page = &pages[flush_cb.page_idx];
		if(page->is_dirty){
			page->state = PAGE_FLUSHING;
			flush_cb.inflying_io++;
			meta_volume->io_submit(page->addr, PF_EC_INDEX_PAGE_SIZE, lut_flush_cbk, page, true);

		}
	}
#endif	
	
}

PfLutPage* PfLutPte::page(PfEcVolumeIndex* owner)
{
	if(pfn == 0){
		PfLutPage* p = owner->get_page();
		pfn = p->pfn(owner);
	}
	return &owner->pages[pfn];
}
int64_t PfLutPte::offset(PfEcVolumeIndex* owner)
{
	assert(owner->rvs_pte > owner->fwd_pte);
	if(this > owner->rvs_pte){
		return (this - owner->rvs_pte)* PF_EC_INDEX_PAGE_SIZE;
	}
	return (this - owner->fwd_pte) * PF_EC_INDEX_PAGE_SIZE;
}
uint32_t PfLutPage::pfn(PfEcVolumeIndex* owner)
{
	return this - &owner->pages[0];
}

//#define _1GB (1LL<<30)
//PteManager* PteManager::create_manager()
//{
//	通过mmap分配内存，将实例分配到地址按1GB对齐的位置，方便后面pte寻址
//
//	int a;
//	void* p = &a;
//	uint64_t GB_mask = ~(_1GB -1);
//
//	void* p1 = NULL;
//	p = p&GB_mask;
//	for(p=p-_1GB; ; p =p- _1GB){
//		p1 = mmap()
//	}
//}