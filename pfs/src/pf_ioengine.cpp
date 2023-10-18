/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */


#include <libaio.h>
#include <sys/prctl.h>
#include <stdint.h>
#include <stdexcept>
#include <liburing.h>

#include "pf_ioengine.h"
#include "pf_message.h"
#include "pf_buffer.h"
#include "pf_flash_store.h"
#include "pf_dispatcher.h" //for IoSubTask
#include "pf_main.h"
#include "pf_spdk.h"

using namespace std;

static uint64_t fd_get_cap(int fd)
{
	struct stat fst;
	int rc = fstat(fd, &fst);
	if(rc != 0)
	{
		rc = -errno;
		S5LOG_ERROR("Failed fstat, rc:%d", rc);
		return rc;
	}
	if(S_ISBLK(fst.st_mode )){
		long number;
		ioctl(fd, BLKGETSIZE, &number);
		return number*512;
	}
	else
	{
		return fst.st_size;
	}
	return 0;
}

PfIoEngine::PfIoEngine(PfFlashStore* d)
{
	disk = d;
	fd = d->fd;
}

uint64_t PfAioEngine::sync_write(void *buffer, uint64_t size, uint64_t offset)
{
	uint64_t rc;

	if ( (rc = pwrite(fd, buffer, size, offset)) != size)
		return -errno;

	return rc;
}

uint64_t PfAioEngine::sync_read(void *buffer, uint64_t size, uint64_t offset)
{
	uint64_t rc;

	if ( (rc = pread(fd, buffer, size, offset)) != size)
		return -errno;

	return rc;
}

uint64_t PfAioEngine::get_device_cap()
{
	return fd_get_cap(fd);
}


int PfAioEngine::init()
{
	S5LOG_INFO("Initing AIO engine for disk:%s", disk->tray_name);
	aio_ctx = NULL;
	int rc = io_setup(MAX_AIO_DEPTH, &aio_ctx);
	if (rc < 0)
	{
		S5LOG_ERROR("io_setup failed, rc:%d", rc);
		throw std::runtime_error(format_string("io_setup failed, rc:%d", rc));
	}
	aio_poller = std::thread(&PfAioEngine::polling_proc, this);
	return 0;
}
//#define SKIP_DISK 1
int PfAioEngine::submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len)
{
#ifdef SKIP_DISK
	io->complete(PfMessageStatus::MSG_STATUS_SUCCESS);
#else
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	//below is the most possible case
	if(IS_READ_OP(io->opcode))
		io_prep_pread(&io->aio_cb, fd, data_bd->buf, media_len, media_offset);
	else
		io_prep_pwrite(&io->aio_cb, fd, data_bd->buf, media_len, media_offset);
	batch_iocb[batch_io_cnt++] = &io->aio_cb;
	if(batch_io_cnt >= (BATCH_IO_CNT>>1)){
		if (batch_io_cnt >= BATCH_IO_CNT) {
			S5LOG_FATAL("Too many fails to submit IO on ssd:%s", this->disk->name);
		}
		return submit_batch();
	}
#endif
	return 0;
}
int PfAioEngine::submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len)
{
	//S5LOG_DEBUG("Begin cow_io %d", io->opcode);
	if (IS_READ_OP(io->opcode))
		io_prep_pread(&io->aio_cb, fd, io->buf, media_len, media_offset);
	else
		io_prep_pwrite(&io->aio_cb, fd, io->buf, media_len, media_offset);
	struct iocb* ios[1] = { &io->aio_cb };
	return io_submit(aio_ctx, 1, ios);
}
int PfAioEngine::submit_batch()
{
	if(batch_io_cnt == 0)
		return 0;
	//S5LOG_DEBUG("batch size:%d", batch_io_cnt);
	int rc = io_submit(aio_ctx, batch_io_cnt, batch_iocb);
	if (rc != batch_io_cnt) {
		S5LOG_ERROR("Failed to submit %d IO, rc:%d", batch_io_cnt, rc);
		return rc;
	}
	batch_io_cnt = 0;
	return 0;
}
#if 0
int PfAioEngine::poll_io(int *completions)
{
	return 0;
}
#endif

void PfAioEngine::polling_proc()
{
#define MAX_EVT_CNT 100
	struct io_event evts[MAX_EVT_CNT];
	char name[32];
	snprintf(name, sizeof(name), "p_aio_%s", disk->tray_name);
	prctl(PR_SET_NAME, name);
	int rc = 0;
	while (1) {
		rc = io_getevents(aio_ctx, 1, MAX_EVT_CNT, evts, NULL);
		if (rc < 0)
		{
			continue;
		}
		else
		{
			for (int i = 0; i < rc; i++)
			{
				struct iocb* aiocb = (struct iocb*)evts[i].obj;
				int64_t len = evts[i].res;
				int64_t res = evts[i].res2;
				IoSubTask* t = pf_container_of(aiocb, struct IoSubTask, aio_cb);
				switch (t->opcode) {
				case S5_OP_READ:
				case S5_OP_WRITE:
				case S5_OP_REPLICATE_WRITE:
					if (unlikely(len != t->parent_iocb->cmd_bd->cmd_bd->length || res < 0)) {
						S5LOG_ERROR("aio error, len:%d rc:%d, op:%d off:0x%llx len:%d, logic offset:0x%llx, buf:%p", (int)len, (int)res,
							aiocb->aio_lio_opcode, aiocb->u.c.offset, aiocb->u.c.nbytes, t->parent_iocb->cmd_bd->cmd_bd->offset,
							aiocb->u.c.buf);
						//res = (res == 0 ? len : res);
						app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);
					}
					else
						t->complete(PfMessageStatus::MSG_STATUS_SUCCESS);
					break;
				case S5_OP_COW_READ:
				case S5_OP_COW_WRITE:
					if (unlikely(len != ((CowTask*)t)->size || res < 0)) {
						S5LOG_ERROR("cow aio error, op:%d, len:%d rc:%d", t->opcode, (int)len, (int)res);
						//res = (res == 0 ? len : res);
						app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);

					}
					else {
						t->complete_status = PfMessageStatus::MSG_STATUS_SUCCESS;
						sem_post(&((CowTask*)t)->sem);
					}

					break;
				default:
					S5LOG_FATAL("Unknown task opcode:%d", t->opcode);
				}

			}
		}
	}
}



int PfIouringEngine::init()
{

	int rc;
	S5LOG_INFO("Initing IoUring engine for disk:%s", disk->tray_name);
	rc = io_uring_queue_init(MAX_AIO_DEPTH, &uring, 0);
	if (rc < 0) {
		S5LOG_ERROR("init_iouring failed, rc:%d", rc);
		throw std::runtime_error(format_string("init_iouring failed, rc:%d", rc));
	}
	int fds[] = { disk->fd };
	rc = io_uring_register_files(&uring, fds, 1);
	if (rc < 0) {
		S5LOG_ERROR("io_uring_register_files failed, rc:%d", rc);
		throw std::runtime_error(format_string("io_uring_register_files failed, rc:%d", rc));
	}

	seg_cnt_per_dispatcher = (int)(app_context.disps[0]->mem_pool.data_pool.buf_size * app_context.disps[0]->mem_pool.data_pool.buf_count / (1 << 30));
	struct iovec v[app_context.disps.size() * seg_cnt_per_dispatcher];
	for (int i = 0; i < app_context.disps.size(); i++) {
		rc = 0;
		struct disp_mem_pool* mem_pool = &app_context.disps[i]->mem_pool;
		//divide buffer into small one, 1GB per buffer
		for(int j=0;j< seg_cnt_per_dispatcher;j++){
			v[i * seg_cnt_per_dispatcher + j].iov_base = (char*)mem_pool->data_pool.data_buf + (j<<30);
			v[i * seg_cnt_per_dispatcher + j].iov_len = j == seg_cnt_per_dispatcher - 1 ? 1 << 30 : mem_pool->data_pool.buf_size * mem_pool->data_pool.buf_count%(1<<30);
		}
	}
	//there's a limit on buffer size, each buffer max 1GB
	rc = io_uring_register_buffers(&uring, v, (unsigned int)S5ARRAY_SIZE(v));
	if (rc)
		S5LOG_ERROR("Failed call io_uring_register_buffers, rc:%d, buffer size:%ld", rc, v[0].iov_len);
	iouring_poller = std::thread(&PfIouringEngine::polling_proc, this);
	return rc;
}

int PfIouringEngine::submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len)
{
	struct BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	struct io_uring_sqe* sqe = io_uring_get_sqe(&uring);
	
	int buf_index =(int) ( (((int64_t)data_bd->buf - (int64_t)app_context.disps[io->parent_iocb->disp_index]->mem_pool.data_pool.data_buf) >> 30)
		+ io->parent_iocb->disp_index * seg_cnt_per_dispatcher);
	if (IS_READ_OP(io->opcode))
		io_uring_prep_read_fixed(sqe, fd, data_bd->buf, (unsigned)media_len, media_offset, buf_index);
	else
		io_uring_prep_write_fixed(sqe, fd, data_bd->buf, (unsigned)media_len, media_offset, buf_index);
	sqe->user_data = (uint64_t)io;
	sqe->fd = 0; //index of registered fd
	sqe->flags |= IOSQE_FIXED_FILE;
	int rc = io_uring_submit(&uring);
	if (rc<0)
		S5LOG_ERROR("Failed io_uring_submit, rc:%d", rc);
	return rc;
}
int PfIouringEngine::submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len)
{
	//TODO: use io_uring linked IO to perform cow read-write in one op
	struct io_uring_sqe* sqe = io_uring_get_sqe(&uring);
#if 0
int PfIouringEngine::poll_io(int *completions)
{
	return 0;
}
#endif

	//can't use fixed buffer , since cow buffer has not been registered
	if (IS_READ_OP(io->opcode))
		io_uring_prep_read(sqe, fd, io->buf, (unsigned)media_len, media_offset);
	else
		io_uring_prep_write(sqe, fd, io->buf, (unsigned)media_len, media_offset);
	sqe->user_data = (uint64_t)io;
	sqe->fd = disk->fd;
	//sqe->flags |= IOSQE_FIXED_FILE;
	int rc = io_uring_submit(&uring);
	if (rc < 0)
		S5LOG_ERROR("Failed io_uring_submit, rc:%d", rc);
	return rc;
}
void PfIouringEngine::polling_proc()
{
	char name[32];
	snprintf(name, sizeof(name), "uring_%s", disk->tray_name);
	prctl(PR_SET_NAME, name);
	int rc = 0;
	struct io_uring_cqe* cqe;
	struct io_uring* r = &uring;
	while (1) {
		rc = io_uring_wait_cqe(r, &cqe);
		if (rc < 0) {
			S5LOG_ERROR("io_uring_wait_cqe:%d: %s\n", rc, strerror(-rc));
			return ;
		}

		int64_t len = cqe->res;
		IoSubTask* t = (struct IoSubTask*)cqe->user_data;
		io_uring_cqe_seen(r, cqe);
		switch (t->opcode) {
		case S5_OP_READ:
		case S5_OP_WRITE:
		case S5_OP_REPLICATE_WRITE:
			//S5LOG_DEBUG("aio complete, cid:%d len:%d rc:%d", t->parent_iocb->cmd_bd->cmd_bd->command_id, (int)len, (int)res);
			if (unlikely(len != t->parent_iocb->cmd_bd->cmd_bd->length )) {
				S5LOG_ERROR("uring error, op:%d req len:%ld res:%ld", t->opcode, t->parent_iocb->cmd_bd->cmd_bd->length, len);
				//res = (res == 0 ? len : res);
				app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);
			}
			else
				t->complete(PfMessageStatus::MSG_STATUS_SUCCESS);
			break;
		case S5_OP_COW_READ:
		case S5_OP_COW_WRITE:
			if (unlikely(len != ((CowTask*)t)->size)) {
				S5LOG_ERROR("cow uring error, op:%d, req len:%ld res:%ld", t->opcode, ((CowTask*)t)->size, len);
				//res = (res == 0 ? len : res);
				app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);

			}
			else {
				t->complete_status = PfMessageStatus::MSG_STATUS_SUCCESS;
				sem_post(&((CowTask*)t)->sem);
			}

			break;
		default:
			S5LOG_FATAL("Unknown task opcode:%d", t->opcode);
		}
	}
}

uint64_t PfIouringEngine::sync_write(void *buffer, uint64_t size, uint64_t offset)
{
	uint64_t rc;

	if ( (rc = pwrite(fd, buffer, size, offset)) != size)
		return -errno;

	return rc;
}

uint64_t PfIouringEngine::sync_read(void *buffer, uint64_t size, uint64_t offset)
{
	uint64_t rc;

	if ( (rc = pread(fd, buffer, size, offset)) != size)
		return -errno;

	return rc;
}


uint64_t PfIouringEngine::get_device_cap()
{
	return fd_get_cap(fd);
}


int PfspdkEngine :: init()
{
	int rc;
	int i;
	struct spdk_nvme_io_qpair_opts opts;

	ns = disk->ns;
	// do in ssd init
	ns->block_size = spdk_nvme_ns_get_extended_sector_size(ns->ns);
	num_qpairs = QPAIRS_CNT;
	qpair = (struct spdk_nvme_qpair **)calloc(num_qpairs, sizeof(struct spdk_nvme_qpair *));
	if (!qpair) {
		S5LOG_ERROR("failed to calloc qpair");
		return -ENOMEM;
	}

	spdk_nvme_ctrlr_get_default_io_qpair_opts(ns->ctrlr, &opts, sizeof(opts));
	opts.delay_cmd_submit = true;
	opts.create_only = true;
	opts.async_mode = true;
	//opts.io_queue_requests = spdk_max(g_opts.io_queue_requests, opts.io_queue_requests);

	group = spdk_nvme_poll_group_create(NULL, NULL);
	if (!group) {
		S5LOG_ERROR("failed to create poll group");
		rc = -EINVAL;
		goto failed;
	}

	for (i = 0; i < num_qpairs; i++) {
		qpair[i] = spdk_nvme_ctrlr_alloc_io_qpair(ns->ctrlr, &opts, sizeof(opts));
		if (qpair[i] == NULL) {
			rc = -EINVAL;
			S5LOG_ERROR("failed to alloc io qpair, i=%d", i);
			goto qpair_failed;
		}

		if (spdk_nvme_poll_group_add(group, qpair[i])) {
			rc = -EINVAL;
			S5LOG_ERROR("failed to add poll group, i=%d", i);
			spdk_nvme_ctrlr_free_io_qpair(qpair[i]);
			goto qpair_failed;
		}

		if (spdk_nvme_ctrlr_connect_io_qpair(ns->ctrlr, qpair[i])) {
			rc = -EINVAL;
			S5LOG_ERROR("failed to connect io qpair, i=%d", i);
			spdk_nvme_ctrlr_free_io_qpair(qpair[i]);
			goto qpair_failed;
		}
	}

	return 0;
qpair_failed:
	for (; i > 0; --i) {
		spdk_nvme_ctrlr_free_io_qpair(qpair[i - 1]);
	}
	spdk_nvme_poll_group_destroy(group);
failed:
	free(qpair);
	return rc;
}

static void spdk_sync_io_complete(void *arg, const struct spdk_nvme_cpl *cpl)
{
	int *result = (int *)arg;

	if (spdk_nvme_cpl_is_pi_error(cpl)) {
		S5LOG_ERROR("failed io on nvme with error (sct=%d, sc=%d)", 
			cpl->status.sct, cpl->status.sc);
		*result = -1;
		return ;
	}

	*result = 1;
	return ;
}

void PfspdkEngine::spdk_nvme_disconnected_qpair_cb(struct spdk_nvme_qpair *qpair, void *poll_group_ctx)
{
	return ;
}

uint64_t PfspdkEngine::sync_write(void *buffer, uint64_t buf_size, uint64_t offset)
{
	int rc;
	int result = 0;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(offset, &lba, buf_size, &lba_cnt) != 0) {
		return -EINVAL;
	}

	rc = spdk_nvme_ns_cmd_write_with_md(ns->ns, qpair[0], buffer, NULL, lba, lba_cnt,
		spdk_sync_io_complete, &result, 0, 0, 0);
	if (rc) {
		S5LOG_ERROR("nvme write failed! rc = %d", rc);
		return rc;
	}

	while (result == 0) {
		rc = spdk_nvme_qpair_process_completions(qpair[0], 1);
		if (rc < 0) {
			S5LOG_ERROR("NVMe io qpair process completion error, rc=%d", rc);
			return rc;
		}
	}

	return buf_size;
}

uint64_t PfspdkEngine::sync_read(void *buffer, uint64_t buf_size, uint64_t offset)
{
	int rc;
	int result = 0;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(offset, &lba, buf_size, &lba_cnt) != 0) {
		return -EINVAL;
	}
	
	rc = spdk_nvme_ns_cmd_read_with_md(ns->ns, qpair[0], buffer, NULL, lba, lba_cnt, 
		spdk_sync_io_complete, &result, 0, 0, 0);
	if (rc) {
		S5LOG_ERROR("nvme read failed! rc = %d", rc);
		return rc;
	}

	while (result == 0) {
		rc = spdk_nvme_qpair_process_completions(qpair[0], 1);
		if (rc < 0) {
			S5LOG_ERROR("NVMe io qpair process completion error, rc=%d", rc);
			return rc;
		}
	}
	
	return buf_size;
}

void PfspdkEngine::spdk_io_complete(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	// todo: io error handle
	struct IoSubTask* io = (struct IoSubTask*)ctx;

	io->complete(PfMessageStatus::MSG_STATUS_SUCCESS);

}

int PfspdkEngine::submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len)
{
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(media_offset, &lba, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	if (IS_READ_OP(io->opcode))
		spdk_nvme_ns_cmd_read_with_md(ns->ns, qpair[1], data_bd->buf, NULL, lba, lba_cnt,
			spdk_io_complete, io, 0, 0, 0);
	else
		spdk_nvme_ns_cmd_write_with_md(ns->ns, qpair[1], data_bd->buf, NULL, lba, lba_cnt, 
			spdk_io_complete, io, 0, 0, 0);


	return 0;
}

static void
spdk_engine_disconnect_cb(struct spdk_nvme_qpair *qpair, void *ctx)
{
	// todo
}

int PfspdkEngine::poll_io(int *completions, void *arg)
{
	int num_completions;
	PfspdkEngine *pthis = (PfspdkEngine *)arg;

	num_completions = spdk_nvme_poll_group_process_completions(pthis->group, 0, spdk_engine_disconnect_cb);

	if (unlikely(num_completions < 0)) {
		S5LOG_ERROR("NVMe io group process completion error, num_completions=%d", num_completions);
		return -1;
	}

	*completions = num_completions;

	return num_completions > 0 ? 0 : 1;
}

uint64_t PfspdkEngine::get_device_cap()
{
	return spdk_nvme_ns_get_size(ns->ns);
}


/*
 * Convert I/O offset and length from bytes to blocks.
 *
 * Returns zero on success or non-zero if the byte parameters aren't divisible by the block size.
 */
uint64_t PfspdkEngine::spdk_nvme_bytes_to_blocks(uint64_t offset_bytes, uint64_t *offset_blocks,
	uint64_t num_bytes, uint64_t *num_blocks)
{
	uint32_t block_size = ns->block_size;
	uint8_t shift_cnt;

	/* Avoid expensive div operations if possible. These spdk_u32 functions are very cheap. */
	if (likely(spdk_u32_is_pow2(block_size))) {
		shift_cnt = spdk_u32log2(block_size);
		*offset_blocks = offset_bytes >> shift_cnt;
		*num_blocks = num_bytes >> shift_cnt;
		return (offset_bytes - (*offset_blocks << shift_cnt)) |
		       (num_bytes - (*num_blocks << shift_cnt));
	} else {
		*offset_blocks = offset_bytes / block_size;
		*num_blocks = num_bytes / block_size;
		return (offset_bytes % block_size) | (num_bytes % block_size);
	}
}

struct simple_copy_context
{
	void *arg;
	void* (*func)(void *arg);
};

int PfspdkEngine::submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len)
{
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(media_offset, &lba, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	if (IS_READ_OP(io->opcode))
		spdk_nvme_ns_cmd_read_with_md(ns->ns, qpair[1], io->buf, NULL, lba, lba_cnt,
			spdk_cow_io_complete, io, 0, 0, 0);
	else
		spdk_nvme_ns_cmd_write_with_md(ns->ns, qpair[1], io->buf, NULL, lba, lba_cnt, 
			spdk_cow_io_complete, io, 0, 0, 0);
}

void PfspdkEngine::spdk_cow_io_complete(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	// todo: io error handle

	sem_post(&((CowTask*)ctx)->sem);
}


int PfspdkEngine::submit_scc(uint64_t media_len, off_t src, off_t dest,
	void* (*scc_cb)(void *ctx), void *arg)
{
	struct spdk_nvme_scc_source_range range;
	uint64_t lba_src, lba_dest, lba_cnt;
	struct simple_copy_context *context =
		(struct simple_copy_context *)malloc(sizeof(struct simple_copy_context));

	context->func = scc_cb;
	context->arg = arg;

	if (spdk_nvme_bytes_to_blocks(src, &lba_src, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	if (spdk_nvme_bytes_to_blocks(dest, &lba_dest, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	range.slba = lba_src;
	range.nlb = lba_cnt;
	spdk_nvme_ns_cmd_copy(ns->ns, qpair[1], &range, 1, lba_dest, scc_complete, context);
}

void PfspdkEngine::scc_complete(void *arg, const struct spdk_nvme_cpl *cpl)
{
	struct simple_copy_context *context = (struct simple_copy_context *)arg;

	context->func(context->arg);

	free(context);
}