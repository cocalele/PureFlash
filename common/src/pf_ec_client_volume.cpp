
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
		
	}
	case EVT_EC_UPDATE_LUT:
		rc = dispatch_ec_update_lut(iocb, (struct pf_ec_wal_entry*)evt->arg_p, intptr(evt->arg_q));
		break;

	case EVT_IO_COMPLETE:
		if (iocb->is_ec_page_swap_io) {
			app_context.disps[iocb->disp_index]->dispatch_ec_page_load_complete(iocb);
			return;
		}

	}
}

int PfEcClientVolume::ec_commit_wal(PfServerIocb* iocb, PfVolume* vol, struct pf_ec_wal_entry* wal)
{

	//reply_io_to_client();
}

int PfEcClientVolume::update_lut(PfClientIocb* iocb, struct PfEcRedologEntry* wal, int64_t aof_offset)
{
	//iocb still in use by update_lut, event reply is send to client.
	//in server.cpp, iocb will be reused for next IO after reply send.


	//int lba_cnt = iocb->data_bd->data_len>>LBA_LENGTH_ORDER;
	//iocb->add_ref(lba_cnt); //reserve for wal update
	//iocb->add_ref(lba_cnt); //reserve for forward_lut
	//iocb->add_ref(lba_cnt); //reserve for reverse_lut


	要不要为每个4K记录一个wal? 还是每个大IO只记录一个wal, wal里面记录下长度就行了
	关键在于每个4K都有自己的old_aof_off, 需要分别记录。 必须在这里记录？ 是否可以在更新Lut的时候更新，而不是在这里记录
	//all wals belongs to single page
	for(size_t i=0;i<iocb->data_bd->data_len;i+=LBA_LENGTH){
		struct PfEcRedologEntry* wal = &wals[i];
		off_t curr_aof_off = aof_offset + i;
		off_t curr_vol_off = iocb->cmd_bd->cmd_bd->offset + i;

		int64_t old_aof_off = vol->ec_index->get_lba_offset(curr_vol_off);
		wal->aof_off = curr_aof_off;
		wal->vol_off = curr_vol_off;

		wal->aof_new_section_index = aof_offset / section_size;
		wal->aof_section_length += LBA_LENGTH;
		wal->aof_old_section_index = old_aof_off / section_size;
		wal->aof_section_garbage_length += LBA_LENGTH;
		

		// Step2. update index 
		ec_commit_wal(wal);
		ec_index->set_forward_lut(iocb->cmd_bd->cmd_bd->offset, aof_offset);
		ec_index->set_reverse_lut(aof_offset, iocb->cmd_bd->cmd_bd->offset);
	}
	PfEcRedologPage *p = PAGE_OF_WAL(wals);
	iocb->ec_wal_waiting_list = p->waiting_io;
	p>waiting_io = iocb;
	//if(iocb->forward_lut_state == Done && )

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
		iocb->current_state = FILLING_WAL;
		//fall through to FILLING_WAL
	case FILLING_WAL:
		
		iocb->current_state = UPDATING_FWD_LUT;
		iocb->current_offset = iocb->cmd_bd->cmd_bd->offset;
		//fall through to UPDATING_FWD_LUT
	case UPDATING_FWD_LUT:
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

	}
}
int PfEcClientVolume::io_write(PfClientIocb* iocb, PfVolume* vol)
{







	return 0;
}

int PfEcClientVolume::on_page_load_complete(PfServerIocb* swap_io)
{
	//TODO: set initial page hot score
	for (PfServerIocb* request_io = swap_io->forward_lut_waiting_list; request_io != NULL; ) {
		PfVolume* vol = app_context.get_opened_volume(request_io->cmd_bd->cmd_bd->vol_id);
		request_io = request_io->next;
		int64_t end_off = request_io->cmd_bd.cmd_bd->offset + request_io->cmd_bd.cmd_bd->length;
		for (; request_io->forward_lut_loop_offset < end_off; request_io->forward_lut_loop_offset += LBA_LENGTH) {
			int64_t delta = request_io->forward_lut_loop_offset - request_io->cmd_bd.cmd_bd->offset;
			RetCode r = vol->ec_index->set_forward_lut(request_io, request_io->cmd_bd.cmd_bd->offset + delta, request_io->new_aof_offset + delta);
			if (r == Yield) {
				S5LOG_FATAL("forward lut upate not complete in 1 page as excepted");
				//break;
			}
		}
		if (request_io->forward_lut_loop_offset >= end_off) {
			request_io->forward_lut_state = Done;
		}
	}


	//    request IO from client:       IO1                  IO2
	//                         +---------+------+   +---------+---------+
	//                         |                |   |                   |
	//                         R1               R2  R3                  R4
	//                         |                |   |                   |
	//                         |                +-+-+                   |
	//                         |                  |                     |
	//                        Page1              Page2                 Page3
	//                         |                  |                     |
	//                        swap IO1           swap IO2              swap IO3
	// 
	//   1. for above case, 2 request IO from client, each is 8K write. So each IO split into 2 4K IO. i.e. total 4 4K IO
	//      need to update reverse LUT. R1, R2, R3, R4
	//   2. R1, R2, R3, R4 located in 3 memory pages: Page1, Page2, Page3. will issue 3 swap IO,
	//   3. request_io has only one `next` ptr, how to link on two list? IO2 link in SIO2 and SIO3?
	//      R3, R4 must be executed in serialized mode, to avoid this
	//old index需要加载多个不同的页才能进行set_reverse_lut操作
	//old index不可能和new index在同一个page里面
	for (PfServerIocb* request_io = swap_io->reverse_lut_waiting_list; request_io != NULL; ) {
		//request_io->reverse_lut_state = PAGE_READY;
		PfVolume* vol = app_context.get_opened_volume(request_io->cmd_bd->cmd_bd->vol_id);

		request_io = request_io->next;
		int64_t end_off = request_io->cmd_bd.cmd_bd->offset + request_io->cmd_bd.cmd_bd->length;
		for (; request_io->reverse_lut_loop_offset < end_off; request_io->reverse_lut_loop_offset += LBA_LENGTH) {
			int64_t delta = request_io->reverse_lut_loop_offset - request_io->cmd_bd.cmd_bd->offset;
			RetCode r = vol->ec_index->set_reverse_lut(request_io, request_io->new_aof_offset + delta, request_io->cmd_bd.cmd_bd->offset + delta);
			if (r == Yield) {
				//return 0; //continue to process next request IO on waiting
			}
		}
		if (request_io->reverse_lut_loop_offset >= end_off) {
			request_io->reverse_lut_state = Done;
		}
	}

	app_context.disps[swap_io->disp_index]->iocb_pool.free(swap_io); //allocated at PfEcVolumeIndex::load_page

}