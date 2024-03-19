/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */


#include <libaio.h>
#include <sys/prctl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/fs.h>
#include <fcntl.h>

#include "pf_utils.h"
#include "pf_ioengine.h"
#include "pf_message.h"
#include "pf_buffer.h"
#include "pf_iotask.h"
#include "pf_app_ctx.h"
using namespace std;

uint64_t fd_get_cap(int fd)
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



uint64_t PfAioEngine::sync_write(void *buffer, uint64_t size, uint64_t offset)
{
	//if(size >= (1<<20)){
	//	S5LOG_WARN("Write large IO:%ldMB!", size>>20);
	//}
	return pwrite(fd, buffer, size, offset);
}

uint64_t PfAioEngine::sync_read(void *buffer, uint64_t size, uint64_t offset)
{
	//if (size >= (1 << 20)) {
	//	S5LOG_WARN("Read large IO:%ldMB!", size >> 20);
	//}

	return pread(fd, buffer, size, offset);
}

uint64_t PfAioEngine::get_device_cap()
{
	return fd_get_cap(fd);
}


int PfAioEngine::init()
{
	S5LOG_INFO("Initing AIO engine for disk:%s", disk_name.c_str());
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

PfAioEngine::~PfAioEngine()
{
	pthread_cancel(aio_poller.native_handle());
	aio_poller.join();
	io_destroy(aio_ctx);
}

//#define SKIP_DISK 1
int PfAioEngine::submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len)
{
#ifdef SKIP_DISK
	io->complete(PfMessageStatus::MSG_STATUS_SUCCESS);
#else
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	//below is the most possible case
	if (IS_READ_OP(io->opcode))
		io_prep_pread(&io->aio_cb, fd, data_bd->buf, media_len, media_offset);
	else
		io_prep_pwrite(&io->aio_cb, fd, data_bd->buf, media_len, media_offset);
	batch_iocb[batch_io_cnt++] = &io->aio_cb;
	if (batch_io_cnt >= (BATCH_IO_CNT>>1)) {
		if (batch_io_cnt >= BATCH_IO_CNT) {
			S5LOG_FATAL("Too many fails to submit IO on ssd:%s", disk_name.c_str());
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
	snprintf(name, sizeof(name), "p_aio_%s", disk_name.c_str());
	prctl(PR_SET_NAME, name);
	int rc = 0;
	while (1) {
		rc = io_getevents(aio_ctx, 1, MAX_EVT_CNT, evts, NULL);
		if (rc < 0) {
			continue;
		} else {
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
						t->ops->complete(t, PfMessageStatus::MSG_STATUS_AIOERROR);
					}
					else
						t->ops->complete(t, PfMessageStatus::MSG_STATUS_SUCCESS);
					break;
				case S5_OP_COW_READ:
				case S5_OP_COW_WRITE:
					if (unlikely(len != ((CowTask*)t)->size || res < 0)) {
						S5LOG_ERROR("cow aio error, op:%d, len:%d rc:%d", t->opcode, (int)len, (int)res);
						//res = (res == 0 ? len : res);
						t->ops->complete(t, PfMessageStatus::MSG_STATUS_AIOERROR);

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

BOOL is_disk_clean(PfIoEngine* eng)
{
	void* buf = align_malloc_spdk(LBA_LENGTH, LBA_LENGTH, NULL);
	BOOL rc = TRUE;
	int64_t* p = (int64_t*)buf;

	if (LBA_LENGTH != eng->sync_read(buf, LBA_LENGTH, 0))
	{
		rc = FALSE;
		goto release1;
	}
	for (uint32_t i = 0; i < LBA_LENGTH / sizeof(int64_t); i++)
	{
		if (p[i] != 0)
		{
			rc = FALSE;
			goto release1;
		}
	}
release1:
	free_spdk(buf);
	return rc;
}


