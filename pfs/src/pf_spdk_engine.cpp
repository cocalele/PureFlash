
#include "pf_ioengine.h"
#include "pf_message.h"
#include "pf_buffer.h"
#include "pf_flash_store.h"
#include "pf_dispatcher.h" //for IoSubTask
#include "pf_main.h"
#include "pf_spdk.h"

#include "pf_spdk_engine.h"
#include "pf_trace_defs.h"
#include "spdk/trace.h"
#include "spdk/env.h"

static __thread  pf_io_channel *tls_io_channel = NULL;

int PfspdkEngine::init()
{
	this->ns = ns;
	return 0;
}

static void spdk_sync_io_complete(void* arg, const struct spdk_nvme_cpl* cpl)
{
	int* result = (int*)arg;

	if (spdk_nvme_cpl_is_pi_error(cpl)) {
		S5LOG_ERROR("failed io on nvme with error (sct=%d, sc=%d)",
			cpl->status.sct, cpl->status.sc);
		*result = -1;
		return;
	}

	*result = 1;
	return;
}

void PfspdkEngine::spdk_nvme_disconnected_qpair_cb(struct spdk_nvme_qpair* qpair, void* poll_group_ctx)
{
	return;
}

/* sync IO and async IO will submit to qp[0];
 * cow will submit to qp[1];
 * it means that, when waiting sync IO complete, async IO completion will also be handled
 * but cow compleion will not be handled
 */
uint64_t PfspdkEngine::sync_write(void* buffer, uint64_t buf_size, uint64_t offset)
{
	int rc;
	int result = 0;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(offset, &lba, buf_size, &lba_cnt) != 0) {
		return -EINVAL;
	}

	rc = spdk_nvme_ns_cmd_write_with_md(ns->ns, tls_io_channel->qpair[0], buffer, NULL, lba, (uint32_t)lba_cnt,
		spdk_sync_io_complete, &result, 0, 0, 0);
	if (rc) {
		S5LOG_ERROR("nvme write failed! rc = %d", rc);
		return rc;
	}

	while (result == 0) {
		rc = spdk_nvme_qpair_process_completions(tls_io_channel->qpair[0], 1);
		if (rc < 0) {
			S5LOG_ERROR("NVMe io qpair process completion error, rc=%d", rc);
			return rc;
		}
	}

	return buf_size;
}

uint64_t PfspdkEngine::sync_read(void* buffer, uint64_t buf_size, uint64_t offset)
{
	int rc;
	int result = 0;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(offset, &lba, buf_size, &lba_cnt) != 0) {
		return -EINVAL;
	}

	rc = spdk_nvme_ns_cmd_read_with_md(ns->ns, tls_io_channel->qpair[0], buffer, NULL, lba, (uint32_t)lba_cnt,
		spdk_sync_io_complete, &result, 0, 0, 0);
	if (rc) {
		S5LOG_ERROR("nvme read failed! rc = %d", rc);
		return rc;
	}

	while (result == 0) {
		rc = spdk_nvme_qpair_process_completions(tls_io_channel->qpair[0], 1);
		if (rc < 0) {
			S5LOG_ERROR("NVMe io qpair process completion error, rc=%d", rc);
			return rc;
		}
	}

	return buf_size;
}

void PfspdkEngine::spdk_io_complete(void* ctx, const struct spdk_nvme_cpl* cpl)
{
	// todo: io error handle
	struct IoSubTask* io = (struct IoSubTask*)ctx;
#ifdef WITH_SPDK_TRACE
	uint64_t complete_tsc = spdk_get_ticks();
	io->reply_time = complete_tsc;
	spdk_poller_trace_record(TRACE_DISK_IO_STAT, get_current_thread()->poller_id, 0, io->parent_iocb->cmd_bd->cmd_bd->offset,
								get_us_from_tsc(complete_tsc - ((PfServerIocb *)io->parent_iocb)->received_time_hz, get_current_thread()->tsc_rate),
								get_us_from_tsc(complete_tsc - io->submit_time, get_current_thread()->tsc_rate));
#endif
	io->ops->complete(io, PfMessageStatus::MSG_STATUS_SUCCESS);
}

int PfspdkEngine::submit_io(struct IoSubTask* io, int64_t media_offset, int64_t media_len)
{
	BufferDescriptor* data_bd = io->parent_iocb->data_bd;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(media_offset, &lba, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	if (IS_READ_OP(io->opcode))
		spdk_nvme_ns_cmd_read_with_md(ns->ns, tls_io_channel->qpair[0], data_bd->buf, NULL, lba, (uint32_t)lba_cnt,
			spdk_io_complete, io, 0, 0, 0);
	else
		spdk_nvme_ns_cmd_write_with_md(ns->ns, tls_io_channel->qpair[0], data_bd->buf, NULL, lba, (uint32_t)lba_cnt,
			spdk_io_complete, io, 0, 0, 0);

	return 0;
}

static void
spdk_engine_disconnect_cb(struct spdk_nvme_qpair* qpair, void* ctx)
{
	// todo
}

int PfspdkEngine::poll_io(int* completions, void* arg)
{
	int num_completions;

	if (!tls_io_channel)
		return 0;

	num_completions = (int)spdk_nvme_poll_group_process_completions(tls_io_channel->group, 0, spdk_engine_disconnect_cb);
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
uint64_t PfspdkEngine::spdk_nvme_bytes_to_blocks(uint64_t offset_bytes, uint64_t* offset_blocks,
	uint64_t num_bytes, uint64_t* num_blocks)
{
	uint32_t block_size = ns->block_size;
	uint32_t shift_cnt;

	/* Avoid expensive div operations if possible. These spdk_u32 functions are very cheap. */
	if (likely(spdk_u32_is_pow2(block_size))) {
		shift_cnt = spdk_u32log2(block_size);
		*offset_blocks = offset_bytes >> shift_cnt;
		*num_blocks = num_bytes >> shift_cnt;
		return (offset_bytes - (*offset_blocks << shift_cnt)) |
			(num_bytes - (*num_blocks << shift_cnt));
	}
	else {
		*offset_blocks = offset_bytes / block_size;
		*num_blocks = num_bytes / block_size;
		return (offset_bytes % block_size) | (num_bytes % block_size);
	}
}

struct simple_copy_context
{
	void* arg;
	void* (*func)(void* arg);
};

int PfspdkEngine::submit_cow_io(struct CowTask* io, int64_t media_offset, int64_t media_len)
{
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(media_offset, &lba, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	int rc = 0;
	if (IS_READ_OP(io->opcode))
		rc = spdk_nvme_ns_cmd_read_with_md(ns->ns, tls_io_channel->qpair[1], io->buf, NULL, lba, (uint32_t)lba_cnt,
			spdk_cow_io_complete, io, 0, 0, 0);
	else
		rc = spdk_nvme_ns_cmd_write_with_md(ns->ns, tls_io_channel->qpair[1], io->buf, NULL, lba, (uint32_t)lba_cnt,
			spdk_cow_io_complete, io, 0, 0, 0);
	return rc;
}

void PfspdkEngine::spdk_cow_io_complete(void* ctx, const struct spdk_nvme_cpl* cpl)
{
	// todo: io error handle

	sem_post(&((CowTask*)ctx)->sem);
}


int PfspdkEngine::submit_scc(uint64_t media_len, off_t src, off_t dest,
	void* (*scc_cb)(void* ctx), void* arg)
{
	struct spdk_nvme_scc_source_range range;
	uint64_t lba_src, lba_dest, lba_cnt;
	struct simple_copy_context* context =
		(struct simple_copy_context*)malloc(sizeof(struct simple_copy_context));

	context->func = scc_cb;
	context->arg = arg;

	if (spdk_nvme_bytes_to_blocks(src, &lba_src, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	if (spdk_nvme_bytes_to_blocks(dest, &lba_dest, media_len, &lba_cnt) != 0) {
		return -EINVAL;
	}

	range.slba = lba_src;
	range.nlb = (uint16_t)lba_cnt;
	return spdk_nvme_ns_cmd_copy(ns->ns, tls_io_channel->qpair[1], &range, 1, lba_dest, scc_complete, context);
}

void PfspdkEngine::scc_complete(void* arg, const struct spdk_nvme_cpl* cpl)
{
	struct simple_copy_context* context = (struct simple_copy_context*)arg;

	context->func(context->arg);

	free(context);
}

int PfspdkEngine::pf_spdk_io_channel_open(int num_qpairs)
{
	struct spdk_nvme_io_qpair_opts opts;
	int rc;
	int i;

	struct pf_io_channel *pic = (struct pf_io_channel *)calloc(1, sizeof(struct pf_io_channel));
	if (!pic) {
		S5LOG_ERROR("Failed to alloc pf_io_channel");
		return -ENOMEM;
	}

	pic->num_qpairs = num_qpairs;
	pic->qpair = (struct spdk_nvme_qpair**)calloc(num_qpairs, sizeof(struct spdk_nvme_qpair*));
	if (!pic) {
		S5LOG_ERROR("Failed to alloc spdk_nvme_qpair");
		free(pic);
		return -ENOMEM;
	}

	spdk_nvme_ctrlr_get_default_io_qpair_opts(ns->ctrlr, &opts, sizeof(opts));
	opts.delay_cmd_submit = true;
	opts.create_only = true;
	opts.async_mode = true;

	pic->group = spdk_nvme_poll_group_create(NULL, NULL);
	if (!pic->group) {
		S5LOG_ERROR("failed to create poll group");
		rc = -EINVAL;
		goto failed;
	}

	for (i = 0; i < num_qpairs; i++) {
		pic->qpair[i] = spdk_nvme_ctrlr_alloc_io_qpair(ns->ctrlr, &opts, sizeof(opts));
		if (pic->qpair[i] == NULL) {
			rc = -EINVAL;
			S5LOG_ERROR("failed to alloc io qpair, i=%d", i);
			goto qpair_failed;
		}

		if (spdk_nvme_poll_group_add(pic->group, pic->qpair[i])) {
			rc = -EINVAL;
			S5LOG_ERROR("failed to add poll group, i=%d", i);
			spdk_nvme_ctrlr_free_io_qpair(pic->qpair[i]);
			goto qpair_failed;
		}

		if (spdk_nvme_ctrlr_connect_io_qpair(ns->ctrlr, pic->qpair[i])) {
			rc = -EINVAL;
			S5LOG_ERROR("failed to connect io qpair, i=%d", i);
			spdk_nvme_ctrlr_free_io_qpair(pic->qpair[i]);
			goto qpair_failed;
		}
	}

	tls_io_channel = pic;
	return 0;

qpair_failed:
	for (; i > 0; --i) {
		spdk_nvme_ctrlr_free_io_qpair(pic->qpair[i-1]);
	}
	spdk_nvme_poll_group_destroy(pic->group);

failed:
	free(pic->qpair);
	free(pic);
	return rc;
}

int PfspdkEngine::pf_spdk_io_channel_close(struct pf_io_channel *pic)
{
	int i;

	if(!pic)
		pic = tls_io_channel;

	if (!pic) {
		S5LOG_ERROR("cannnot find io channel in current thread");
		return -EINVAL;
	}

	for (i = 0; i < pic->num_qpairs; i++) {
		spdk_nvme_ctrlr_free_io_qpair(pic->qpair[i]);
	}

	spdk_nvme_poll_group_destroy(pic->group);
	free(pic->qpair);
	free(pic);
	tls_io_channel = NULL;

	return 0;
}
