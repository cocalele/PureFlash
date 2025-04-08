#include "pf_utils.h"
#include "pf_message.h"
#include "pf_client_api.h"
#include "pf_client_priv.h"
#include "pf_aof.h"
#include "pf_ec_client_volume.h"
#include "pf_coroutine.h"
#include "pf_ec_wal.h"
#include "pf_ec_volume_index.h"

int do_io_write(PfClientIocb* iocb, PfEcClientVolume* vol);
int PfEcClientVolume::do_open(bool reopen, bool is_aof)
{
	if(reopen){
		S5LOG_FATAL("reopen on ec volume not expected");
	} else {
		Cleaner _c;
		std::string dv_name = volume_name + "^data";
		data_volume = pf_open_aof(dv_name.c_str(), snap_name.c_str(), O_RDWR, cfg_file.c_str(), S5_LIB_VER);
		if(data_volume == NULL){
			S5LOG_ERROR("Failed open data volume:%s, rc:%d", dv_name.c_str(), -1);
			return -1;
		}
		_c.push_back([this](){pf_close_aof(data_volume);});

		std::string mv_name = volume_name + "^meta";
		meta_volume = (PfReplicatedVolume*) pf_open_volume(mv_name.c_str(), cfg_file.c_str(), snap_name.c_str(), S5_LIB_VER);
		if (meta_volume == NULL) {
			S5LOG_ERROR("Failed open meta volume:%s, rc:%d", mv_name.c_str(), -1);
			return -1;
		}
		_c.push_back([this]() {pf_close_volume(meta_volume); });

		runtime_ctx = meta_volume->runtime_ctx;
		runtime_ctx->add_ref();
		event_queue = runtime_ctx->vol_proc->event_queue;

		head_buf = aligned_alloc(LBA_LENGTH, LBA_LENGTH);
		if(head_buf == NULL){
			S5LOG_ERROR("Failed to alloc memory for head");
			return -ENOMEM;
		}
		memset(head_buf, 0, LBA_LENGTH);

		int page_cnt = 2000;//TODO: 可以使用更灵活的方法决定内存页的数量，比如与volume size相关，已达到内存命中率与内存占用量的平衡
		void *p = malloc(sizeof(PfEcVolumeIndex) + sizeof(PfLutPage)*page_cnt);
		if(p == NULL){
			S5LOG_ERROR("Failed to alloc volume index");
			return -ENOMEM;
		}
		ec_index = new(p) PfEcVolumeIndex(this, page_cnt);
		_c.push_back([this]{delete ec_index;});
		ec_redolog = new PfEcRedolog();
		_c.push_back([this] {delete ec_redolog; });
		int rc = ec_redolog->init(this, meta_volume);
		if (rc) {
			S5LOG_ERROR("Failed init redolog");
			return rc;
		}
		flush_routine = co_create(runtime_ctx->vol_proc, std::bind(&PfEcClientVolume::co_flush, this));
		if (flush_routine == NULL) {
			S5LOG_ERROR("Failed to create flush coroutine");
			return -ENOMEM;
		}

		_c.cancel_all();
	}
	return 0;
}
PfEcClientVolume::~PfEcClientVolume(){
	S5LOG_ERROR("Not implemented, resouce leak!");
}
static int unalign_io_print_cnt_ec = 0;
int PfEcClientVolume::io_submit(void* buf, size_t length, off_t offset,
	ulp_io_handler callback, void* cbk_arg, int is_write)
{


		// Check request params
	if (unlikely((offset & SECT_SIZE_MASK) != 0 || (length & SECT_SIZE_MASK) != 0 )) {
		S5LOG_ERROR("Invalid offset:%ld or length:%ld", offset, length);
		return -EINVAL;
	}
	if(unlikely((offset & 0x0fff) || (length & 0x0fff)))	{
		unalign_io_print_cnt_ec ++;
		if((unalign_io_print_cnt_ec % 1000) == 1) {
			S5LOG_WARN("Unaligned IO on volume:%s OP:%s offset:0x%lx len:0x%lx, num:%d", volume_name.c_str(),
			           is_write ? "WRITE" : "READ", offset, length, unalign_io_print_cnt_ec);
		}
	}
	auto io = runtime_ctx->iocb_pool.alloc();
	if (io == NULL){
		S5LOG_WARN("IOCB pool empty, EAGAIN!");
		return -EAGAIN;
	}
	//S5LOG_INFO("Alloc iocb:%p, data_bd:%p", io, io->data_bd);
	//assert(io->data_bd->client_iocb != NULL);
	io->volume = this;
	io->ulp_handler = callback;
	io->ulp_arg = cbk_arg;

	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;
	io->submit_time = now_time_usec();
	io->user_buf = buf;
	io->user_iov_cnt = 0;
	memcpy(io->data_bd->buf, buf, length);
	io->data_bd->data_len = (int)length;
	cmd->opcode = is_write ? S5_OP_WRITE : S5_OP_READ;
	//cmd->vol_id = volume->volume_id;
	cmd->buf_addr = (__le64)io->data_bd->buf;
	cmd->rkey = 0;
	cmd->offset = offset;
	cmd->length = (uint32_t)length;
	//cmd->snap_seq = volume->snap_seq;
	int rc = event_queue->post_event( EVT_EC_IO_REQ, 0, io, this);
	if (rc)
		S5LOG_ERROR("Failed to submmit io, rc:%d", rc);
	return rc;
}
int PfEcClientVolume::iov_submit(const struct iovec* iov, const unsigned int iov_cnt, size_t length, off_t offset,
	ulp_io_handler callback, void* cbk_arg, int is_write)
{
	S5LOG_ERROR("Not implemented");
	return -ENOTSUP;
}

int PfEcClientVolume::process_event(int event_type, int arg_i, void* arg_p)
{
	S5LOG_INFO("ec volume get event:%d", event_type);
	switch (event_type)
	{
	case EVT_EC_IO_REQ:
	{
		PfClientIocb* io = (PfClientIocb*)arg_p;
		if(io->cmd_bd->cmd_bd->opcode == S5_OP_WRITE){
			io->current_state = APPENDING_AOF;
			do_io_write(io, this);
		}
		

	}
	break;
	case EVT_EC_FLUSH_META:
	{
		if(meta_in_flushing){
			S5LOG_WARN("A meta flush is still ongoig");
			break;
		}
		co_enter(flush_routine);
	}
	break;
	case EVT_EC_FLUSH_META_COMPLETE:
	{
		//TODO: resend pending IO
		S5LOG_FATAL("Not implemented, should not happen");
	}
	
	}
}

void PfEcClientVolume::co_flush(/*bool initial */)
{
	while (1) {
		if (meta_in_flushing) {
			S5LOG_INFO("A flushing is on going");
			co_yield();
		}
		meta_in_flushing = true;
		//ec_redolog->set_log_phase(); //

		volatile int rc = ec_index->co_flush_once();
		if(rc){
			S5LOG_ERROR("Failed flush volume index, rc:%d", rc);
		}
		memcpy(head_buf, &head, sizeof(head));

		rc = meta_volume->co_pwrite(&head_buf, LBA_LENGTH, 0);
		
		if (rc) {
			S5LOG_ERROR("Failed flush volume head, rc:%d", rc);
		}
		meta_in_flushing = false;
		event_queue->post_event(EVT_EC_FLUSH_META_COMPLETE, rc, NULL, this);
		co_yield();
	}
}

int do_io_write(PfClientIocb* iocb, PfEcClientVolume* vol)
{
	int rc = 0;
	switch(iocb->current_state) {
	case APPENDING_AOF:
	{
		struct PfEcRedologEntry* wal = vol->ec_redolog->alloc_entry();
		if (wal == NULL) {
			assert(0);
		}
		{
			off_t curr_pos = vol->data_volume->file_length();
			rc = (int)vol->data_volume->append(iocb->data_bd->buf, iocb->data_bd->data_len);
			if (rc < 0) {
				S5LOG_ERROR("Failed call ec_write_data, rc:%d", (int)curr_pos);
				return (int)curr_pos;
			}
			wal->aof_off = curr_pos;
			wal->vol_off = iocb->cmd_bd->cmd_bd->offset;
			iocb->wal = wal;
			iocb->current_state = FILLING_WAL;
		}
	}
		//fall through to FILLING_WAL
	case FILLING_WAL:
		
		iocb->current_state = UPDATING_FWD_LUT;
		iocb->current_offset = iocb->cmd_bd->cmd_bd->offset;
		//fall through to UPDATING_FWD_LUT
	case UPDATING_FWD_LUT:
	{
		struct PfEcRedologEntry* wal = iocb->wal;
		for(;iocb->current_offset < iocb->cmd_bd->cmd_bd->offset + iocb->cmd_bd->cmd_bd->length; iocb->current_offset += LBA_LENGTH) {
			PfLutPte* pte = vol->ec_index->get_fwd_pte(PF_OFF2PTE_INDEX(iocb->current_offset));
			PfLutPage* page = pte->page(vol->ec_index);
			if (page->state != PAGE_PRESENT) {
				vol->ec_index->load_page(iocb, page);
				return 1;
			}
			int64_t old_off = vol->ec_index->get_forward_lut(iocb->current_offset);
			int idx = (iocb->current_offset - iocb->cmd_bd->cmd_bd->offset)>>LBA_LENGTH_ORDER;
			wal->old_section_index[idx] = PF_SECTION_INDEX(old_off);
			//TODO: update garbage length of each section
			vol->ec_index->set_forward_lut(iocb->current_offset, iocb->new_aof_offset + (iocb->current_offset - iocb->cmd_bd->cmd_bd->offset));
		}
		iocb->current_state = UPDATING_RVS_LUT;
		iocb->current_offset = iocb->new_aof_offset;
	}
		//fall through to update reverse LUT
	case UPDATING_RVS_LUT:
		for (; iocb->current_offset < iocb->new_aof_offset + iocb->cmd_bd->cmd_bd->length;
			iocb->current_offset += LBA_LENGTH) {
			PfLutPte* pte = vol->ec_index->get_rvs_pte(PF_OFF2PTE_INDEX(iocb->current_offset));
			PfLutPage* page = pte->page(vol->ec_index);
			if (page->state != PAGE_PRESENT) {
				vol->ec_index->load_page(iocb, page);
				return 1;
			}
			vol->ec_index->set_reverse_lut(iocb->current_offset, iocb->cmd_bd->cmd_bd->offset + (iocb->current_offset - iocb->new_aof_offset));

		}
		iocb->current_state = COMMITING_WAL;
		
		vol->ec_redolog->commit_entry(iocb->wal, iocb);
		
	}
}

void PfEcClientVolume::close(){

	S5LOG_INFO("close volume:%s", volume_name.c_str());
	state = VOLUME_CLOSED;


	runtime_ctx->remove_volume(this);
	runtime_ctx->dec_ref();


}