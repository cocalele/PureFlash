#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/hdreg.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/idr.h>

#include "s5k_basetype.h"
#include "s5k_log.h"
#include "s5k_message.h"
#include "s5ioctl.h"
#include "s5k_imagectx.h"


/* maximum number of minors, =1 for disks that can't be partitioned. */

#define MINORS 16
#define MAX_DMA_LENGTH                 0x7f
#define B2S_SHIFT	3  //12-9,  2^12=4096, 2^9=512

extern int32_t s5bd_major;
extern struct miscdevice s5bd_mngt_miscdev;

/*statistic io for filling to /proc/diskstats*/

static struct hd_struct* s5bd_get_part_from_s5dev(struct s5_imagectx *s5bd_dev)
{
	if(!s5bd_dev)
		return NULL;
	else
		return &s5bd_dev->disk->part0;
}

static void s5bd_start_io_acct(struct s5_imagectx *s5bd_dev, struct bio *bio)
{
	int32_t rw = 0;
	int32_t cpu;
	struct hd_struct* part = NULL;
	if (bio == NULL)
		return;
	rw = bio_data_dir(bio);
	part = s5bd_get_part_from_s5dev(s5bd_dev);
	if(!part)
		return;
	cpu = part_stat_lock();
	part_round_stats(cpu, part);
	part_stat_inc(cpu, part, ios[rw]);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 2)
	part_stat_add(cpu, part, sectors[rw], bio->bi_size >> 9);
#else
	part_stat_add(cpu, part, sectors[rw], bio->bi_iter.bi_size >> 9);
#endif
	(void) cpu; /* The macro invocations above want the cpu argument, I do not like
		       the compiler warning about cpu only assigned but never used... */
	part_inc_in_flight(part, rw);
	part_stat_unlock();
}

void s5bd_end_io_acct(struct s5_imagectx *s5bd_dev, volatile struct bio *bio, uint32_t id)
{
	int32_t rw = 0;
	int32_t cpu;
	unsigned long duration = 0;
	struct hd_struct* part = NULL;
	if (bio == NULL)
		return;

	rw = bio_data_dir(bio);
	duration = jiffies - s5bd_dev->tidinfo[id].start_jiffies;
	part = s5bd_get_part_from_s5dev(s5bd_dev);
	if(!part)
		return;

	cpu = part_stat_lock();
	part_stat_add(cpu, part, ticks[rw], duration);
	part_round_stats(cpu, part);
	part_dec_in_flight(part, rw);
	part_stat_unlock();
}


static void s5bd_bio_request(struct request_queue *q, struct bio *bio)
{
	struct s5_imagectx* ictx = (struct s5_imagectx*)q->queuedata;
#if 0

	//LOG_INFO("s5bd_bio_request bio %p \n", bio);
   // if (!bio_data_dir(bio))
	{
		bio_endio(bio, 0);
		return 0;
	}
#endif
	if (unlikely(ictx->kill_all_bio))
	{
		bio_endio(bio, -EIO);
		LOG_ERROR("End bio with error for kill_all_bio is true.");
		return;
	}

	if (bio_data_dir(bio))
	{
		atomic_inc(&ictx->dinfo.dstat.bio_write_accepted);
	}
	else
	{
		atomic_inc(&ictx->dinfo.dstat.bio_read_accepted);
	}
	spin_lock(&ictx->lock_bio);
	bio_list_add(&ictx->send_bio_list, bio);
	spin_unlock(&ictx->lock_bio);

	s5bd_start_io_acct(ictx, bio);

	wake_up(&ictx->send_kthread_wq);

	return ;
}

static int32_t s5bd_ioctl(struct block_device *bdev, fmode_t mode, uint32_t cmd, unsigned long arg)
{
	struct hd_geometry geo;
	uint64_t vol_size, sect;
	struct s5_imagectx *s5bd_dev = (struct s5_imagectx*)bdev->bd_disk->private_data;

	vol_size = s5bd_dev->volume_size;
	sect = vol_size / LBA_LENGTH;
	switch (cmd)
	{
	case HDIO_GETGEO:
		geo.heads = 1;
		geo.sectors = 8;
		geo.cylinders = sect >> 3;
		if(copy_to_user((void*)arg, &geo, sizeof(geo)))
			return -EFAULT;
		return 0;
	}
	return -ENOTTY;
}

static int32_t s5bd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	uint64_t volume_size, sect;
	struct s5_imagectx* ictx = (struct s5_imagectx*)bdev->bd_disk->private_data;

	volume_size = ictx->volume_size;
	sect = volume_size / LBA_LENGTH;

	/* The CHS is not useful for NAND flash, so assume the head to 1,
	 * sectors to 8 and others are set to cylinders.
	 * Currently, the partitions is not considered.
	 */
	geo->heads = 1;
	geo->sectors = 8;
	geo->cylinders = sect >> 3;

	return 0;
}

static int s5bd_blk_open(struct block_device *bdev, fmode_t mode)
{
	struct s5_imagectx *ictx = (struct s5_imagectx*)bdev->bd_disk->private_data;
	kref_get(&ictx->kref);
	return 0;
}

static void s5bd_free_dev(struct kref* kref)
{
	struct s5_imagectx *ictx = container_of(kref, struct s5_imagectx, kref);
	LOG_TRACE("The refcount of device(%s) is %d.", ictx->dinfo.dev_name, atomic_read(&ictx->kref.refcount));
}

static void s5bd_blk_release(struct gendisk *disk, fmode_t mode)
{
	struct s5_imagectx *ictx = (struct s5_imagectx*)disk->private_data;
	kref_put(&ictx->kref, s5bd_free_dev);
	return;
}

static void s5bd_init_dev_ops(struct s5_imagectx* ictx)
{
	struct block_device_operations *fops = &ictx->fops;
	ictx->disk->fops = fops;
	fops->ioctl = s5bd_ioctl;
	fops->compat_ioctl = s5bd_ioctl;
	fops->getgeo = s5bd_getgeo;
	fops->open = s5bd_blk_open;
	fops->release = s5bd_blk_release;

	fops->owner = s5bd_mngt_miscdev.fops->owner;
}

int32_t s5bd_volume_init(struct s5_imagectx* ictx, int32_t id)
{
	int32_t ret = 0;
	uint64_t volume_size = 0;
	struct device* pdev = NULL;
	struct hd_struct *ppart = NULL;
	struct partition_meta_info *pinfo = NULL;

	if (id < 0 || id > MAX_DEVICE_NUM)
	{
		LOG_ERROR("Invalid id(%d)", id);
		return -EINVAL;
	}

	ret = s5bd_open(ictx);
	if (ret)
	{
		LOG_ERROR("Failed to initialize device(%s), ret(%d).",
			ictx->dinfo.dev_name, ret);
		goto out;
	}
	else
		LOG_INFO("Succeed to initialize device(%s) id(%lld) size(%lld)."
			, ictx->dinfo.dev_name, ictx->volume_id, ictx->volume_size);

	volume_size = ictx->volume_size;
	ictx->queue = blk_alloc_queue(GFP_KERNEL);
	if(!ictx->queue)
	{
		LOG_ERROR("Failed to call blk_alloc_queue.");
		unregister_blkdev(s5bd_major, "s5bd");
		ret = -ENOMEM;
		goto release_s5bd;
	}

	LOG_INFO("Succeed to call blk_alloc_queue.");

	ictx->queue->queue_flags = QUEUE_FLAG_DEFAULT;
#if 0 /* TRIM support */
	set_bit(QUEUE_FLAG_DISCARD,&ictx->queue->queue_flags);
	ictx->queue->limits.discard_granularity = 4096;
	ictx->queue->limits.max_discard_sectors  = MTIP_MAX_TRIM_ENTRY_LEN * MTIP_MAX_TRIM_ENTRIES;
	ictx->queue->limits.discard_zeroes_data = 0;
#endif
	queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES, ictx->queue);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, ictx->queue);
	blk_queue_make_request(ictx->queue, s5bd_bio_request);
	ictx->queue->queuedata = ictx;

	ictx->disk = alloc_disk(0);
	if(!ictx->disk)
	{
		blk_cleanup_queue(ictx->queue);
		unregister_blkdev(s5bd_major, "s5bd");
		LOG_ERROR("Failed to call alloc_disk.");
		ret = -ENOMEM;
		goto release_s5bd;
	}
	LOG_INFO("Succeed to call alloc_disk.");

	blk_queue_logical_block_size(ictx->queue, 512);
	blk_queue_physical_block_size(ictx->queue, 512);
	blk_queue_io_min(ictx->queue, 4096);
	blk_queue_io_opt(ictx->queue, 4096);
	blk_queue_max_hw_sectors(ictx->queue, 8 * MAX_4K_CNT);
	ictx->queue->limits.discard_granularity = 4096;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ictx->queue);
	blk_queue_max_discard_sectors(ictx->queue, 8);
	blk_queue_bounce_limit(ictx->queue, BLK_BOUNCE_ANY);
	blk_queue_dma_alignment(ictx->queue, 7);
	blk_queue_max_segments(ictx->queue, (MAX_DMA_LENGTH - 1) << B2S_SHIFT);
	blk_queue_max_segment_size(ictx->queue, 4096);
	//blk_queue_max_hw_sectors(ictx->queue, (MAX_DMA_LENGTH - 1) << B2S_SHIFT);

	ictx->disk->major = s5bd_major;
	ictx->disk->minors = MINORS;
	ictx->disk->first_minor = id*MINORS;

	kref_init(&ictx->kref);

	s5bd_init_dev_ops(ictx);

	ictx->disk->private_data = ictx;
	ictx->disk->queue = ictx->queue;

	strncpy(ictx->disk->disk_name, ictx->dinfo.dev_name, DISK_NAME_LEN - 1);
	LOG_TRACE("Trace: disk_name = %s. volume_size %llu.", ictx->disk->disk_name, volume_size);
	set_capacity(ictx->disk, volume_size >> 9);
	snprintf(ictx->attr.uuid, sizeof(ictx->attr.uuid), "NB-%s-%02llx",
			ictx->dinfo.dev_name, ictx->volume_id);
	snprintf(ictx->attr.name, sizeof(ictx->attr.name), "NB-WQ-%s-%02llx",
			ictx->dinfo.dev_name, ictx->volume_id);

	pdev = disk_to_dev(ictx->disk);
	if(pdev)
	{
		ppart = dev_to_part(pdev);
		if(ppart)
		{
RETRY:
			pinfo = ppart->info;
			if(pinfo)
			{
				snprintf(pinfo->uuid, sizeof(pinfo->uuid), "%s-%02llx",
					ictx->dinfo.dev_name, ictx->volume_id);
				LOG_TRACE("Trace: partition meta info vol:%s uuid:%s.", pinfo->volname, pinfo->uuid);
			}
			else
			{
				ppart->info = vzalloc(sizeof(*ppart->info));
				goto RETRY;
			}
		}
		else
			LOG_ERROR("Failed to get ppart dev_to_part.");
	}
	else
		LOG_ERROR("Failed to get pdev disk_to_dev.");

	add_disk(ictx->disk);

	goto out;

release_s5bd:
	s5bd_close(ictx);
out:
	return ret;
}

int32_t s5bd_volume_exit(struct s5_imagectx* ictx, int32_t id)
{
	int32_t ret = 0;
	if (!ictx)
	{
		LOG_ERROR("Invalid ictx.");
		return -EINVAL;
	}

	if (id < 0 || id > MAX_DEVICE_NUM)
	{
		LOG_ERROR("Invalid id(%d)", id);
		return -EINVAL;
	}

	if(!blk_queue_dying(ictx->queue))
		blk_cleanup_queue(ictx->queue);
	if (ictx->disk->flags & GENHD_FL_UP)
		del_gendisk(ictx->disk);

	LOG_INFO("Succeed to unregister block device(%s).", ictx->dinfo.dev_name);

	ret = s5bd_close(ictx);

	LOG_INFO("Call s5bd_close ret(%d).", ret);

	return ret;
}

