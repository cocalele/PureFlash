#include "pf_ec_volume_index.h"

PfLutPte() : waiting_list(&PfClientIocb::ec_next) {}

static void page_load_cbk(void* cbk_arg, int complete_status)
{

	if(complete_status != PfMessageStatus::MSG_STATUS_SUCCESS){
		S5LOG_ERROR("page load failed, rc:%d", complete_status);
	}
	PfLutPte* pte = (PfLutPte*)ckb_arg;
	
	PfEcClientVolume* vol = pte->owner_volume();
	for (PfClientIocb* p = pte->waiting_list.head; p != NULL; p = p->ec_next) {
		do_io_write(io, pte->owner);
	}
	pte->waiting_list.clear();
}
//1. 实现coroutine
//2. 像android上实现模态对话框一样，进行内部的事件循环，外部逻辑连续
//3. .net await模式，或者c++ promise模式。 这个模式需要外部线程池，不行
//4. EC context, 状态机. 我们使用这一种，
int PfEcVolumeIndex::load_page(PfClientIocb* client_io, PfLutPte* pte)
{

	assert(pte->state != PAGE_PRESENT);
	pte->waiting_list.push_back(client_io);
	int rc = 0;
	if (pte->state == PAGE_LOADING )
	{
		return 0;
	}
	rc = pf_io_submit_read(meta_volume, PF_EC_INDEX_PAGE_SIZE, pte->page->addr, page_load_cbk, pte);
	if(rc){
		S5LOG_ERROR("Failed to submit read IO, volume:%s offset:%ld", meta_volume->volume_name.c_str(), pte->page->addr);
		return rc;
	}
	return 0;
}
int PfEcVolumeIndex::set_forward_lut(PfServerIocb* iocb, int64_t vol_offset, int64_t aof_offset)
{
	lut[vol_offset>>LBA_LENGTH_ORDER]=aof_offset; //update data in memory
	lut_page.status=dirty;//mark page dirty
}
int PfEcVolumeIndex::set_reverse_lut(int64_t vol_offset, int64_t aof_offset);

int64_t PfEcVolumeIndex::get_lba_offset(int64_t);
