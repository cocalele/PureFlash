
int PfEcClientVolume::do_open(bool reopen, bool is_aof)
{
	int rc = 0;
	if(reopen){
		Cleaner _c;
		rc = data_volume->do_open(reopen, true);
		if(rc){
			S5LOG_ERROR("Failed to open ec data volume:%s", volume_name.c_str());
			return rc;
		}
		_c.push_back([this](){data_volume->close();});
		rc = meta_volume->do_open(reopen, false);
		if (rc) {
			S5LOG_ERROR("Failed to open ec data volume:%s", volume_name.c_str());
			return rc;
		}
		_c.cancel_all();
		runtime_ctx = data_volume->runtime_ctx;
		event_queue = runtime_ctx->event_queue;
	} else {
		data_volume = pf_open_aof(volume_name + "^data", cfg_file, snap_name, S5_LIB_VER);
		meta_volume = pf_open_volume(volume_name+"^meta", cfg_file, snap_name, S5_LIB_VER);
	}
	return 0;
}

int PfEcClientVolume::pf_io_submit(struct PfClientVolume* volume, void* buf, size_t length, off_t offset,
	ulp_io_handler callback, void* cbk_arg, int is_write)
{


		// Check request params
	if (unlikely((offset & SECT_SIZE_MASK) != 0 || (length & SECT_SIZE_MASK) != 0 )) {
		S5LOG_ERROR("Invalid offset:%ld or length:%ld", offset, length);
		return -EINVAL;
	}
	if(unlikely((offset & 0x0fff) || (length & 0x0fff)))	{
		unalign_io_print_cnt ++;
		if((unalign_io_print_cnt % 1000) == 1) {
			S5LOG_WARN("Unaligned IO on volume:%s OP:%s offset:0x%lx len:0x%x, num:%d", volume->volume_name.c_str(),
			           is_write ? "WRITE" : "READ", offset, length, unalign_io_print_cnt);
		}
	}
	auto io = volume->runtime_ctx->iocb_pool.alloc();
	if (io == NULL){
		S5LOG_WARN("IOCB pool empty, EAGAIN!");
		return -EAGAIN;
	}
	//S5LOG_INFO("Alloc iocb:%p, data_bd:%p", io, io->data_bd);
	//assert(io->data_bd->client_iocb != NULL);
	io->volume = volume;
	io->ulp_handler = callback;
	io->ulp_arg = cbk_arg;

	struct PfMessageHead *cmd = io->cmd_bd->cmd_bd;
	io->submit_time = now_time_usec();
	io->user_buf = buf;
	io->user_iov_cnt = 0;
	memcpy(io->data_bd->buf, buf, length);
	io->data_bd->data_len = (int)length;
	cmd->opcode = is_write ? S5_OP_WRITE : S5_OP_READ;
	cmd->vol_id = volume->volume_id;
	cmd->buf_addr = (__le64)io->data_bd->buf;
	cmd->rkey = 0;
	cmd->offset = offset;
	cmd->length = (uint32_t)length;
	cmd->snap_seq = volume->snap_seq;
	int rc = volume->event_queue->post_event( EVT_EC_IO_REQ, 0, io, volume);
	if (rc)
		S5LOG_ERROR("Failed to submmit io, rc:%d", rc);
	return rc;
}
int PfEcClientVolume::pf_iov_submit(struct PfClientVolume* volume, const struct iovec* iov, const unsigned int iov_cnt, size_t length, off_t offset,
	ulp_io_handler callback, void* cbk_arg, int is_write)
{

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
	
		

	}
}

int PfEcClientVolume::ec_commit_wal(PfServerIocb* iocb, PfVolume* vol, struct pf_ec_wal_entry* wal)
{

	//reply_io_to_client();
}

int do_io_write(PfClientIocb* iocb, PfEcClientVolume* vol)
{
	switch(iocb->current_state) {
	case APPENDING_AOF:
		struct PfEcRedologEntry* wal = app_context.ec_redolog->alloc_entry();
		if (wal == NULL) {
			assert(0);
		}
		off_t curr_pos = data_volume->file_length();
		int rc = data_volume->append(iocb->data_bd->buf, iocb->data_bd->data_len);
		if (rc < 0) {
			S5LOG_ERROR("Failed call ec_write_data, rc:%d", (int)off);
			return (int)off;
		}
		wal->aof_off = curr_pos;
		wal->vol_off = iocb->cmd_bd->cmd_bd->offset;
		iocb->wal = wal;
		iocb->current_state = FILLING_WAL;
		//fall through to FILLING_WAL
	case FILLING_WAL:
		
		iocb->current_state = UPDATING_FWD_LUT;
		iocb->current_offset = iocb->cmd_bd->cmd_bd->offset;
		//fall through to UPDATING_FWD_LUT
	case UPDATING_FWD_LUT:
		struct PfEcRedologEntry* wal = iocb->wal;
		for(;iocb->current_offset < iocb->cmd_bd->cmd_bd->offset + iocb->cmd_bd->cmd_bd->length; iocb->current_offset += LBA_LENGTH) {
			PfLutPte* pte = ec_index->get_fwd_pte(PF_OFF2PTE_INDEX(iocb->current_offset));
			if (pte->status != PAGE_PRESENT) {
				ec_index->load_page(iocb, pte);
				return 1;
			}
			int64_t old_off = vol->ec_index->get_lba_offset(iocb->current_offset);
			int idx = (iocb->current_offset - iocb->cmd_bd->cmd_bd->offset)>>LBA_LENGTH_ORDER;
			wal->old_section_index[idx] = PF_SECTION_INDEX(old_off);
			//TODO: update garbage length of each section
			vol->ec_index->set_forward_lut(iocb->current_offset, iocb->new_aof_offset + (iocb->current_offset - iocb->cmd_bd->cmd_bd->offset));
		}
		iocb->current_state = UPDATING_RVS_LUT;
		iocb->current_offset = iocb->new_aof_offset;
		//fall through to update reverse LUT
	case UPDATING_RVS_LUT:
		for (; iocb->current_offset < iocb->new_aof_offset + iocb->cmd_bd->cmd_bd->length;
			iocb->current_offset += LBA_LENGTH) {
			PfLutPte* pte = ec_index->get_rvs_pte(PF_OFF2PTE_INDEX(iocb->current_offset));
			if (pte->status != PAGE_PRESENT) {
				ec_index->load_page(iocb, pte);
				return 1;
			}
			vol->ec_index->set_reverse_lut(iocb->current_offset, iocb->cmd_bd->cmd_bd->offset + (iocb->current_offset - iocb->new_aof_offset));

		}
		iocb->current_state = COMMITING_WAL;
		
		PfEcRedologPage* wal_page = PAGE_OF_WAL(iocb->wal);
		wal_page->waiting_io.push_back(iocb);
	}
}
