#include <sys/prctl.h>
#include <pthread.h>
#include "pf_coroutine.h"
#include "pf_ec_wal.h"
#include "pf_ec_volume_index.h"
#include "pf_ec_client_volume.h"

static void wal_write_cbk(void* cbk_arg, int complete_status);

#define CHECK_WAL_INTERVAL_US 1000
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

	//定时提交ec redolog
	commit_timer = std::thread([this]() {
		prctl(PR_SET_NAME, "wal_timer");
		std::function<int()> commit_fn = std::function<int()>([this]()->int {
			if (current_page == NULL)
				return 0;
			current_page->time_to_commit -= CHECK_WAL_INTERVAL_US;
			if (current_page->time_to_commit <= 0) {
				PfEcRedologPage* page = current_page;
				current_page = NULL;


				int rc = meta_volume->io_submit(page, sizeof(*page), page->offset, wal_write_cbk, page, true);
				if (rc) {
					S5LOG_FATAL("Failed post wal to event queue, rc:%d", rc);
				}
			}
			return 0;
		});

		while (1)
		{
			if (usleep(CHECK_WAL_INTERVAL_US) != 0)
				return;

			if (current_page == NULL)
				continue;
			owner_volume->event_queue->sync_invoke(commit_fn);
		}
		});
		return 0;
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
	if(current_page->free_index == ENTRY_PER_PAGE){
		PfEcRedologPage* page = current_page;
		current_page = NULL;
		
		//写入redolog， 这里和server端处理ssd的redolog不同，这里是异步写入, 因此当运行到下面代码时这个提交IO可能还没完成
		int rc = meta_volume->io_submit(page, sizeof(*page), page->offset, wal_write_cbk, page, true);
		if(rc){
			S5LOG_FATAL("Failed post wal to event queue, rc:%d", rc);
		}
		current_zone->inflying_commit_io ++;
		if (current_zone->current_offset >= current_zone->end_offset) {
			int oppsite_idx = current_zone == &zone[0] ? 1 : 0;
			//owner_volume->head.current_redolog = (int8_t)oppsite_idx;
			Zone* oppsite_zone = &zone[oppsite_idx];

			if(oppsite_zone->need_flush || oppsite_zone->inflying_commit_io != 0 || owner_volume->meta_in_flushing ){
				//前一次的刷盘还在进行中
				S5LOG_FATAL("Unexpected error, previous meta flush is ongoing!");
			}
			current_zone->need_flush = 1; //当前区域满了，切换到另外一个区域

			++owner_volume->head.redolog_phase;
			owner_volume->ec_redolog->set_current(owner_volume->head.redolog_phase % 2);
			owner_volume->section_tbl->set_current(owner_volume->head.redolog_phase % 2);
			//co_enter(owner_volume->flush_routine); //为避免有inflying的redolog write, 这里不启动flush, 而是在前面设置了need_flush, 等redolog page写完了再开始flush
		}
	}
}

int PfEcRedolog::co_replay(int64_t current_phase)
{
	assert(current_phase == owner_volume->head.redolog_phase);
	assert(current_page == NULL);
	PfEcRedologPage* page = free_pages.pop_front();
	DeferCall _d([this, page]()->void{free_pages.push_back(page);});
	off_t start_off = current_phase %2 == 0 ? owner_volume->head.redolog_position_first : owner_volume->head.redolog_position_second;
	
	int pg_cnt = 0;
	int entry_cnt = 0;
	S5LOG_INFO("Begin replay redolog phase:%ld", current_phase);
	static_assert(sizeof(*page) == PAGE_SIZE);

	for(off_t redolog_off = start_off; redolog_off < start_off + PF_EC_REDOLOG_SIZE; redolog_off += PAGE_SIZE) {
		int rc = meta_volume->co_pread(page, sizeof(*page), redolog_off); 
		if (rc) {
			S5LOG_FATAL("Failed post wal to event queue, rc:%d", rc);
		}

		if(page->phase != current_phase){
			S5LOG_DEBUG("Stop replay on get phase:%ld", page->phase);
			break;
		}
		pg_cnt ++;
		entry_cnt += page->free_index;
		for (int i = 0; i < page->free_index; i++){
			PfEcRedologEntry* e = &page->entries[i];
			//UPDATE forward and reverse lut, and section table
			int section_idx = 0;
			for (off_t off = 0; off < e->length; off += LBA_LENGTH, section_idx ++) {
				owner_volume->ec_index->co_sync_set_forward_lut(e->vol_off + off, e->aof_off + off);
				owner_volume->section_tbl->current->items[e->old_section_index[section_idx]].garbage_length++;
				owner_volume->section_tbl->current->items[PF_SECTION_INDEX(e->aof_off + off)].length++;
			}
			//把set lut操作分成两个循环来做，以减少缺页的可能
			for (off_t off = 0; off < e->length; off += LBA_LENGTH, section_idx++) {
				owner_volume->ec_index->co_sync_set_reverse_lut(e->aof_off + off, e->vol_off + off);
			}
		}
	}
	S5LOG_INFO("replay complete, %d pages %d entries", pg_cnt, entry_cnt);
	return 0;
}
int PfEcRedologPage::init(int64_t phase, off_t offset)
{
	this->phase = phase;
	this->offset = offset;
	free_index = 0;
	time_to_commit = 1000; //in us(micro second)
	waiting_io.clear();
	return 0;
}


int PfEcSectionInfoTable::co_flush_once()
{
	int64_t old_phase = owner_volume->head.redolog_phase;
	SectionZone* old = &zone[old_phase%2];
	assert(current->phase == old->phase +1 );

	//pwrite old table
	//void* buf = aligned_alloc(PAGE_SIZE, LBA_LENGTH);
	//if(buf == NULL){
	//	return -ENOMEM;
	//}
	//DeferCall _c([buf](){free (buf);});

	void* buf = owner_volume->head_buf; //reuse head buf
	memcpy(buf, old, sizeof(SectionZone));
	off_t off = (old_phase % 2 == 0) ? owner_volume->head.section_tbl_position_first : owner_volume->head.section_tbl_position_second;
	volatile int rc = owner_volume->meta_volume->co_pwrite(buf, LBA_LENGTH, 
		off);
	if(rc){
		S5LOG_ERROR("Failed to write section table on %ld, rc:%d", off, rc);
	}
	return rc;
}
void PfEcSectionInfoTable::set_current(int64_t idx) {
	assert(current != &zone[(int)idx]);
	S5LOG_DEBUG("Copy old section table to current, new current index:%ld", idx);
	memcpy(&zone[(int)idx], current, sizeof(SectionZone));
	current = &zone[(int)idx];
	current->phase = owner_volume->head.redolog_phase;
	assert(owner_volume->head.redolog_phase % 2 == idx);
}

