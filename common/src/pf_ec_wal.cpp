#include "pf_coroutine.h"
#include "pf_ec_wal.h"
#include "pf_ec_client_volume.h"

int PfEcRedolog::init(PfEcClientVolume* owner, PfReplicatedVolume* meta)
{
	owner_volume = owner;
	meta_volume = meta;
	current_page = NULL;
	free_pages.push_back(&pages[0]);
	free_pages.push_back(&pages[1]);
	entry_pool.init(256);

	zone[0].current_offset=0;
	zone[0].start_offset = owner->head.redolog_position_first;
	zone[0].end_offset = zone[0].start_offset + owner_volume->head.redolog_size;
	zone[0].need_flush = false;
	zone[0].inflying_commit_io = 0;
	zone[0].phase = 0;

	zone[1].current_offset = 0;
	zone[1].start_offset = owner->head.redolog_position_second;
	zone[1].end_offset = zone[1].start_offset + owner_volume->head.redolog_size;
	zone[1].need_flush = false;
	zone[1].inflying_commit_io = 0;
	zone[1].phase = 0;

}


PfEcRedologEntry* PfEcRedolog::alloc_entry()
{
	
	return entry_pool.alloc();
}

//int PfEcRedolog::set_log_phase(int64_t _phase, uint64_t offset)
//{
//	phase = _phase;
//	start_offset = offset;
//	current_offset = start_offset;
//	end_offset = start_offset + owner_volume->head.redolog_size;
//	return 0;
//}

static void wal_write_cbk(void* cbk_arg, int complete_status)
{
	PfEcRedologPage* page = (PfEcRedologPage*)cbk_arg;

	if (complete_status != 0) {
		S5LOG_ERROR("Failed to write redo log page:%p offset:0x%lx, rc:%d", cbk_arg, page->offset, complete_status);
	}
	for (PfClientIocb* io = page->waiting_io.head; io != NULL; io = io->ec_next) {
		io->ulp_handler(io->ulp_arg, complete_status); //complete client IO
	}
	page->waiting_io.clear();

	PfEcRedolog* redolog = page->owner_volume->ec_redolog;
	int zone_index = page->offset >= page->owner_volume->head.redolog_position_second ? 1 : 0;
	PfEcRedolog::Zone* zone = &redolog->zone[zone_index];
	zone->inflying_commit_io--;
	if(zone->inflying_commit_io == 0 && zone->need_flush){
		co_enter(page->owner_volume->flush_routine);
	}
}

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
void PfEcRedolog::commit_entry(PfEcRedologEntry* e, PfClientIocb* io)
{

	if(current_page == NULL){
		current_page = free_pages.pop_front();
		if (current_page == NULL) {
			S5LOG_FATAL("No free redolog page to use");
		}
		current_page->init(current_zone->phase, current_zone->current_offset);
		current_zone->current_offset += PfEcRedologPage::PageSize;
	}
	current_page->waiting_io.push_back(io);
	//e->phase = current_zone->phase;
	current_page->entries[current_page->free_index++] = *e;
	if(current_page->free_index == ARRAY_SIZE(current_page->entries)){
		PfEcRedologPage* page = current_page;
		current_page = NULL;
		

		int rc = meta_volume->io_submit(page, sizeof(*page), page->offset, wal_write_cbk, page, true);
		if(rc){
			S5LOG_FATAL("Failed post wal to event queue, rc:%d", rc);
		}
		current_zone->inflying_commit_io ++;
		if (current_zone->current_offset >= current_zone->end_offset) {
			Zone* oppsite_zone = current_zone == &zone[0] ? &zone[1] : &zone[0];

			if(oppsite_zone->need_flush || oppsite_zone->inflying_commit_io != 0 || owner_volume->meta_in_flushing ){
				//前一次的刷盘还在进行中
				S5LOG_FATAL("Unexpected error, previous meta flush is ongoing!");
			}
			current_zone->need_flush = 1; //当前区域满了，切换到另外一个区域
			current_zone = oppsite_zone;
			++owner_volume->head.redolog_phase;
			current_zone->phase = owner_volume->head.redolog_phase;
		}
	}

	//PfEcRedologPage* page = PAGE_OF_WAL(e);
	

}

int PfEcRedologPage::init(int phase, off_t offset)
{
	this->phase = phase;
	this->offset = offset;
	free_index = 0;
	waiting_io.clear();
	return 0;
}
