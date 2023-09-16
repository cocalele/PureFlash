#include <stdexcept>
#include <pthread.h>
#include <sys/prctl.h>

#include "pf_utils.h"
#include "pf_log.h"
#include "pf_main.h"
#include "pf_app_ctx.h"
#include "pf_iouring_engine.h"

int PfIouringEngine::init()
{

	int rc;
	S5LOG_INFO("Initing IoUring engine for disk:%s", disk_name.c_str());
	rc = io_uring_queue_init(MAX_AIO_DEPTH, &uring, 0);
	if (rc < 0) {
		S5LOG_ERROR("init_iouring failed, rc:%d", rc);
		throw std::runtime_error(format_string("init_iouring failed, rc:%d", rc));
	}
	int fds[] = { fd };
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
		for (int j = 0; j < seg_cnt_per_dispatcher; j++) {
			v[i * seg_cnt_per_dispatcher + j].iov_base = (char*)mem_pool->data_pool.data_buf + (j << 30);
			v[i * seg_cnt_per_dispatcher + j].iov_len = j == seg_cnt_per_dispatcher - 1 ? 1 << 30 : mem_pool->data_pool.buf_size * mem_pool->data_pool.buf_count % (1 << 30);
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
	PfServerIocb* parent_iocb = (PfServerIocb * )io->parent_iocb;
	int buf_index = (int)((((int64_t)data_bd->buf - (int64_t)app_context.disps[parent_iocb->disp_index]->mem_pool.data_pool.data_buf) >> 30)
		+ parent_iocb->disp_index * seg_cnt_per_dispatcher);
	if (IS_READ_OP(io->opcode))
		io_uring_prep_read_fixed(sqe, fd, data_bd->buf, (unsigned)media_len, media_offset, buf_index);
	else
		io_uring_prep_write_fixed(sqe, fd, data_bd->buf, (unsigned)media_len, media_offset, buf_index);
	sqe->user_data = (uint64_t)io;
	sqe->fd = 0; //index of registered fd
	sqe->flags |= IOSQE_FIXED_FILE;
	int rc = io_uring_submit(&uring);
	if (rc < 0)
		S5LOG_ERROR("Failed io_uring_submit, rc:%d", rc);
	return rc;
}
int PfIouringEngine::submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len)
{
	//TODO: use io_uring linked IO to perform cow read-write in one op
	struct io_uring_sqe* sqe = io_uring_get_sqe(&uring);


	//can't use fixed buffer , since cow buffer has not been registered
	if (IS_READ_OP(io->opcode))
		io_uring_prep_read(sqe, fd, io->buf, (unsigned)media_len, media_offset);
	else
		io_uring_prep_write(sqe, fd, io->buf, (unsigned)media_len, media_offset);
	sqe->user_data = (uint64_t)io;
	sqe->fd = fd;
	//sqe->flags |= IOSQE_FIXED_FILE;
	int rc = io_uring_submit(&uring);
	if (rc < 0)
		S5LOG_ERROR("Failed io_uring_submit, rc:%d", rc);
	return rc;
}
void PfIouringEngine::polling_proc()
{
	char name[32];
	snprintf(name, sizeof(name), "uring_%s", disk_name.c_str());
	prctl(PR_SET_NAME, name);
	int rc = 0;
	struct io_uring_cqe* cqe;
	struct io_uring* r = &uring;
	while (1) {
		rc = io_uring_wait_cqe(r, &cqe);
		if (rc < 0) {
			S5LOG_ERROR("io_uring_wait_cqe:%d: %s\n", rc, strerror(-rc));
			return;
		}

		int64_t len = cqe->res;
		IoSubTask* t = (struct IoSubTask*)cqe->user_data;
		io_uring_cqe_seen(r, cqe);
		switch (t->opcode) {
		case S5_OP_READ:
		case S5_OP_WRITE:
		case S5_OP_REPLICATE_WRITE:
			//S5LOG_DEBUG("aio complete, cid:%d len:%d rc:%d", t->parent_iocb->cmd_bd->cmd_bd->command_id, (int)len, (int)res);
			if (unlikely(len != t->parent_iocb->cmd_bd->cmd_bd->length)) {
				S5LOG_ERROR("uring error, op:%d req len:%ld res:%ld", t->opcode, t->parent_iocb->cmd_bd->cmd_bd->length, len);
				//res = (res == 0 ? len : res);
				app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR);
			}
			else
				t->ops->complete(t, PfMessageStatus::MSG_STATUS_SUCCESS);
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

uint64_t PfIouringEngine::sync_write(void* buffer, uint64_t size, uint64_t offset)
{
	uint64_t rc;

	if ((rc = pwrite(fd, buffer, size, offset)) != size)
		return -errno;

	return rc;
}

uint64_t PfIouringEngine::sync_read(void* buffer, uint64_t size, uint64_t offset)
{
	uint64_t rc;

	if ((rc = pread(fd, buffer, size, offset)) != size)
		return -errno;

	return rc;
}


uint64_t PfIouringEngine::get_device_cap()
{
	return fd_get_cap(fd);
}

