
#include "pf_ioengine.h"
#include "pf_message.h"
#include "pf_buffer.h"
#include "pf_flash_store.h"
#include "pf_dispatcher.h" //for IoSubTask
#include "pf_main.h"
#include "pf_spdk.h"

#include "pf_spdk_engine.h"

int PfspdkEngine::init()
{
	int rc;
	int i;
	struct spdk_nvme_io_qpair_opts opts;

	this->ns = ns;
	// do in ssd init
	ns->block_size = spdk_nvme_ns_get_extended_sector_size(ns->ns);
	num_qpairs = QPAIRS_CNT;
	qpair = (struct spdk_nvme_qpair**)calloc(num_qpairs, sizeof(struct spdk_nvme_qpair*));
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

uint64_t PfspdkEngine::sync_write(void* buffer, uint64_t buf_size, uint64_t offset)
{
	int rc;
	int result = 0;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(offset, &lba, buf_size, &lba_cnt) != 0) {
		return -EINVAL;
	}

	rc = spdk_nvme_ns_cmd_write_with_md(ns->ns, qpair[0], buffer, NULL, lba, (uint32_t)lba_cnt,
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

uint64_t PfspdkEngine::sync_read(void* buffer, uint64_t buf_size, uint64_t offset)
{
	int rc;
	int result = 0;
	uint64_t lba, lba_cnt;

	if (spdk_nvme_bytes_to_blocks(offset, &lba, buf_size, &lba_cnt) != 0) {
		return -EINVAL;
	}

	rc = spdk_nvme_ns_cmd_read_with_md(ns->ns, qpair[0], buffer, NULL, lba, (uint32_t)lba_cnt,
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

void PfspdkEngine::spdk_io_complete(void* ctx, const struct spdk_nvme_cpl* cpl)
{
	// todo: io error handle
	struct IoSubTask* io = (struct IoSubTask*)ctx;

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
		spdk_nvme_ns_cmd_read_with_md(ns->ns, qpair[1], data_bd->buf, NULL, lba, (uint32_t)lba_cnt,
			spdk_io_complete, io, 0, 0, 0);
	else
		spdk_nvme_ns_cmd_write_with_md(ns->ns, qpair[1], data_bd->buf, NULL, lba, (uint32_t)lba_cnt,
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
	PfspdkEngine* pthis = (PfspdkEngine*)arg;

	num_completions = (int)spdk_nvme_poll_group_process_completions(pthis->group, 0, spdk_engine_disconnect_cb);

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
		rc = spdk_nvme_ns_cmd_read_with_md(ns->ns, qpair[1], io->buf, NULL, lba, (uint32_t)lba_cnt,
			spdk_cow_io_complete, io, 0, 0, 0);
	else
		rc = spdk_nvme_ns_cmd_write_with_md(ns->ns, qpair[1], io->buf, NULL, lba, (uint32_t)lba_cnt,
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
	return spdk_nvme_ns_cmd_copy(ns->ns, qpair[1], &range, 1, lba_dest, scc_complete, context);
}

void PfspdkEngine::scc_complete(void* arg, const struct spdk_nvme_cpl* cpl)
{
	struct simple_copy_context* context = (struct simple_copy_context*)arg;

	context->func(context->arg);

	free(context);
}
