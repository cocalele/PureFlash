#include "pf_ec_volume_index.h"

//1. ʵ��coroutine
//2. ��android��ʵ��ģ̬�Ի���һ���������ڲ����¼�ѭ�����ⲿ�߼�����
//3. .net awaitģʽ������c++ promiseģʽ�� ���ģʽ��Ҫ�ⲿ�̳߳أ�����
//4. EC context, ״̬��. ����ʹ����һ�֣�
void PfEcVolumeIndex::load_page(PfServerIocb* client_io, int64_t offset, PfIndexPage* page)
{
	int rc = 0;
	PfServerIocb* io = app_context.disps[disp_index].iocb_pool.alloc(); //free at PfDispatcher::dispatch_ec_page_load_complete(PfServerIocb* swap_io)
	if (unlikely(io == NULL)) {
		S5LOG_ERROR("Failed to allock IOCB for recovery read");
		return -EAGAIN;
	}
	io->submit_time = now_time_usec();
	struct PfMessageHead* cmd = io->cmd_bd->cmd_bd;

	assert(io->data_bd);
	
	
	//io->data_bd->cbk_data = io;
	assert(io->data_bd->buf_capacity >= PF_EC_INDEXER_PAGE_SIZE);
	
	cmd->opcode = PfOpCode::S5_OP_READ;
	cmd->buf_addr = (__le64)page->addr;
	cmd->vol_id = vol_id;
	cmd->rkey = 0;
	cmd->offset = offset;
	cmd->length = (uint32_t)PF_EC_INDEXER_PAGE_SIZE;
	cmd->snap_seq = SNAP_SEQ_HEAD;
	cmd->meta_ver = persist_volume->meta_ver;

	if (spdk_engine_used())
		((PfSpdkQueue*)(conn->dispatcher->event_queue))->post_event_locked(EVT_IO_REQ, 0, io);
	else
		conn->dispatcher->event_queue->post_event(EVT_IO_REQ, 0, io); //for read

	return rc;
}
int PfEcVolumeIndex::set_forward_lut(PfServerIocb* iocb, int64_t vol_offset, int64_t aof_offset)
{
	inner_read();//read lut data from disk to memory
	lut[vol_offset>>LBA_LENGTH_ORDER]=aof_offset; //update data in memory
	lut_page.status=dirty;//mark page dirty
}
int PfEcVolumeIndex::set_reverse_lut(int64_t vol_offset, int64_t aof_offset);

int64_t PfEcVolumeIndex::get_lba_offset(int64_t);
