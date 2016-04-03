#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <net/tcp.h>

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/idr.h>

#include "s5k_basetype.h"
#include "s5k_log.h"
#include "s5k_imagectx.h"


//#define INTERNAL_DEBUG

#ifdef INTERNAL_DEBUG
typedef uint8_t ip_t[MAX_IP_LENGTH];
typedef struct debug_bd_param
{
    ip_t        toe_ip;			///toe ip
} debug_bd_param_t;

static uint32_t disk_numbers = 16;
static uint32_t volume_id_base = 0;
static uint32_t s5_tcp_port_base = 0;
static uint64_t volume_size =  64 * (1024*1024*1024UL); //N*1GB_SIZE

debug_bd_param_t bd_param[]=
{
  /* toe sever ip --- image id */
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"},
	{"10.10.222.146"}
};

#endif

#define INVALID_ID (-1)

struct s5_imagectx* s5bd_mngt_dev[MAX_DEVICE_NUM];

uint32_t s5bd_major = 0;
static long s5bd_mngt_ioctl(struct file *file, uint32_t cmd, unsigned long user);
static int32_t s5bd_map_bd(struct ioctlparam* param);
static int32_t s5bd_unmap_bd(struct ioctlparam* param);
static int32_t s5bd_list_bd(struct ioctlparam* param);
extern int32_t s5bd_volume_init(struct s5_imagectx* ictx, int32_t id);
extern int32_t s5bd_volume_exit(struct s5_imagectx* ictx, int32_t id);

static struct file_operations s5bd_ctl_fops =
{
	.owner	= THIS_MODULE,
	.unlocked_ioctl = s5bd_mngt_ioctl,
};

struct miscdevice s5bd_mngt_miscdev =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICENAME,
	.fops = &s5bd_ctl_fops
};



/**
 * \brief s5bd_mngt_ioctl
 * Register function for ioctl.
 * process the following cmd:
 * MAP_DEVICE
 * UNMAP_DEVICE
 * LIST_DEVICE
 *
 * \param[in] file
 * \param[in] cmd
 * \param[in] user
 *
 * \return errno
 */

static long s5bd_mngt_ioctl(struct file *file, uint32_t cmd, unsigned long user)
{
    struct ioctlparam param;
	int32_t ret = 0;

	if (_IOC_TYPE(cmd) != S5BDMONITOR_CODE)
	{
		LOG_WARN("Unknown s5ioctl command:0x%x.", cmd);
		ret = -ENOTTY;
		goto out;
	}

	switch (cmd)
	{
	case MAP_DEVICE:
		ret = copy_from_user(&param, (struct ioctlparam*)user, sizeof(struct ioctlparam));
		if (ret)
		{
			LOG_ERROR("Failed to copy from user, cmd(MAP_DEVICE).");
		}
        ret = s5bd_map_bd(&param);
        if(ret)
		{
			LOG_ERROR("Failed to map block device %s.", param.bdev.dinfo.dev_name);
		}
        param.retval = ret;
		ret = copy_to_user((struct ioctlparam*)user, &param, sizeof(struct ioctlparam));
		if (ret)
		{
			LOG_ERROR("Failed to copy to user, cmd(MAP_DEVICE).");
		}
		break;
	case UNMAP_DEVICE:
		ret = copy_from_user(&param, (struct ioctlparam*)user, sizeof(struct ioctlparam));
		if (ret)
		{
			LOG_ERROR("Failed to copy from user, cmd(UNMAP_DEVICE).");
		}
        ret = s5bd_unmap_bd(&param);
        if(ret)
		{
			LOG_ERROR("Failed to unmap block device %s.", param.bdev.dinfo.dev_name);
		}
        param.retval = ret;
		ret = copy_to_user((struct ioctlparam*)user, &param, sizeof(struct ioctlparam));
		if (ret)
		{
			LOG_ERROR("Failed to copy to user, cmd(UNMAP_DEVICE).");
		}
		break;
	case LIST_DEVICE:
		ret = copy_from_user(&param, (struct ioctlparam*)user, sizeof(struct ioctlparam));
		if (ret)
		{
			LOG_ERROR("Failed to copy from user, cmd(LIST_DEVICE).");
		}
        ret = s5bd_list_bd(&param);
        if(ret)
		{
			LOG_ERROR("Failed to list block device %s.", param.bdev.dinfo.dev_name);
		}
        param.retval = ret;
		ret = copy_to_user((struct ioctlparam*)user, &param, sizeof(struct ioctlparam));
		if (ret)
		{
			LOG_ERROR("Failed to copy to user, cmd(LIST_DEVICE).");
		}
		break;
	default:
		LOG_WARN("Unknown ioctl command[0x%x].", cmd);
		ret = -ENOTTY;
		break;
	}

out:
	if(ret == 0)
	{
		LOG_TRACE("Execute ioctl successfully.");
	}
	return ret;
}

static int32_t s5bd_map_bd(struct ioctlparam * param)
{
	int32_t ret = BDD_MSG_STATUS_OK;
	int32_t id = INVALID_ID;
#ifndef INTERNAL_DEBUG
	int32_t rc = 0;
#endif
	struct s5_imagectx* ictx = NULL;

	id = param->bdev.bddev_id;

	S5ASSERT(s5bd_mngt_dev[id] == NULL);

	ictx = kzalloc(sizeof(struct s5_imagectx), GFP_KERNEL);
	if (ictx == NULL)
	{
		ret = BDD_MSG_STATUS_BDD_NOMEM;
		LOG_ERROR("Failed to kzalloc struct s5bd_device.");
		goto out;
	}
	memcpy(&ictx->dinfo, &param->bdev.dinfo, sizeof(struct device_info));
#if 1
	S5ASSERT(atomic_read(&ictx->dinfo.dstat.bio_read_accepted) == 0);
	S5ASSERT(atomic_read(&ictx->dinfo.dstat.bio_read_finished_ok) == 0);
	S5ASSERT(atomic_read(&ictx->dinfo.dstat.bio_read_finished_error) == 0);
	S5ASSERT(atomic_read(&ictx->dinfo.dstat.bio_write_accepted) == 0);
	S5ASSERT(atomic_read(&ictx->dinfo.dstat.bio_write_finished_ok) == 0);
	S5ASSERT(atomic_read(&ictx->dinfo.dstat.bio_write_finished_error) == 0);
#endif

//	ictx->snap_seq = param->snap_seq;//latest snap sequence
//	ictx->iops_per_GB = param->iops_density;

	ret = s5bd_volume_init(ictx, id);
	if(ret)
	{
		LOG_ERROR("Failed to call function s5bd_volume_init, ret(%d).", ret);
		kfree(ictx);
        param->retval = ret;
	}
	else
	{
		s5bd_mngt_dev[id] = ictx;
		LOG_INFO("Succeed to map to /dev/%s.", ictx->dinfo.dev_name);
        param->retval = 0;
		ret = BDD_MSG_STATUS_OK;
#ifndef INTERNAL_DEBUG
		rc = try_module_get(s5bd_mngt_miscdev.fops->owner);
		LOG_TRACE("The refcount of management device is %d.", rc);
#endif
	}

out:
	return ret;
}

static int32_t s5bd_unmap_bd(struct ioctlparam* param)
{
	int32_t ret = BDD_MSG_STATUS_OK;
	struct s5_imagectx* ictx = NULL;
	struct block_device *bdev;
	int32_t id = param->bdev.bddev_id;

	//defaut? fixme
	int32_t partno = 0;

	if (id < 0 || id > MAX_DEVICE_NUM)
	{
		ret = BDD_MSG_STATUS_DEVICE_NON_EXISTS;
		LOG_ERROR("Invalid id(%d)", id);
		goto out;
	}

	if (!s5bd_mngt_dev[id])
	{
		ret = BDD_MSG_STATUS_DEVICE_NON_EXISTS;
		LOG_ERROR("Invalid id(%d)", id);
		goto out;
	}

	ictx = s5bd_mngt_dev[id];

	LOG_INFO("Start to unmap deviceid(%d)-devicename(%s).",
			id, ictx->dinfo.dev_name);

	bdev = bdget_disk(ictx->disk, partno);
	/* Do not reset an active device! */
	if (bdev && bdev->bd_holders)
	{
		LOG_WARN("Device(%s) is in use(%d).", ictx->disk->disk_name, bdev->bd_holders);
		ret = -EBUSY;
		goto out;
	}

#ifndef INTERNAL_DEBUG
	if(atomic_read(&ictx->kref.refcount) > 1)
	{
		LOG_WARN("Device(%s) is in use, ref(%d).",
				ictx->disk->disk_name, atomic_read(&ictx->kref.refcount));
		return -EAGAIN;
	}
#endif

	ret = s5bd_volume_exit(ictx, id);
	if(ret)
	{
		s5bd_mngt_dev[id] = ictx;
		LOG_ERROR("Failed to call s5bd_volume_exit, device(%s) ret(%d).",
				ictx->dinfo.dev_name, ret);
		//ret = ERRNO_S5BD_EXIT_FAILED;
		goto out;
	}
	else
	{
		LOG_INFO("Finish unmap deiveid(%d)-devicename(%s).",
				id, ictx->dinfo.dev_name);
	}

	kfree(ictx);
	s5bd_mngt_dev[id] = NULL;
#ifndef INTERNAL_DEBUG
	module_put(s5bd_mngt_miscdev.fops->owner);
#endif
out:
	return ret;
}

static int32_t s5bd_list_bd(struct ioctlparam* param)
{
	int32_t ret = BDD_MSG_STATUS_OK;
	struct s5_imagectx* ictx = NULL;
	int32_t id = param->bdev.bddev_id;
	uint32_t send_bio_list_size = 0;

	if (id < 0 || id > MAX_DEVICE_NUM || !s5bd_mngt_dev[id])
	{
		ret = BDD_MSG_STATUS_DEVICE_NON_EXISTS;
		LOG_ERROR("Invalid id(%d)", id);
		goto out;
	}

	ictx = s5bd_mngt_dev[id];

//	LOG_INFO("Start to list deviceid(%d)-devicename(%s).",
//			id, ictx->dinfo.dev_name);

	spin_lock(&ictx->lock_bio);
	send_bio_list_size = bio_list_size(&ictx->send_bio_list);
	spin_unlock(&ictx->lock_bio);
	atomic_set(&ictx->dinfo.dstat.send_list_len, send_bio_list_size);
	atomic_set(&ictx->dinfo.dstat.retry_fifo_len, kfifo_len(&ictx->timeout_retry_fifo)/sizeof(int32_t));
	atomic_set(&ictx->dinfo.dstat.tid_len, TID_DEPTH - kfifo_len(&ictx->id_generator)/sizeof(int32_t));
	atomic_set(&ictx->dinfo.dstat.finish_id_len, TID_DEPTH - kfifo_len(&ictx->finish_id_generator)/sizeof(int32_t));

	memcpy(&param->bdev.dinfo, &ictx->dinfo, sizeof(struct device_info));

//	LOG_INFO("Finish to list deviceid(%d)-devicename(%s).",
//			id, ictx->dinfo.dev_name);

out:
	return ret;
}

/**
 * \brief s5bd_mngt_dev_init
 * entry function
 *
 * \return errno
 */
static int32_t __init s5bd_mngt_dev_init(void)
{
	int32_t ret = 0;
	int32_t major;
#ifdef INTERNAL_DEBUG
	struct ioctlparam param;
#endif

	ret = misc_register(&s5bd_mngt_miscdev);
	if(ret)
	{
		LOG_ERROR("Failed to register device[%s].", s5bd_mngt_miscdev.name);
	}
	else
	{
		LOG_INFO("Succeed to register device[%s].", s5bd_mngt_miscdev.name);
	}

	major = register_blkdev(s5bd_major, "s5bd");
	if (major <= 0)
	{
		LOG_ERROR("Failed to register_blkdev major(%d).", major);
		misc_deregister(&s5bd_mngt_miscdev);
		return -1;
	}
	else
	{
		s5bd_major = major;
		LOG_INFO("Succeed to register block device major(%d).", s5bd_major);
	}

#ifdef INTERNAL_DEBUG
	uint32_t i;
	for (i = 0; i < disk_numbers; i ++)
	{
		memset(&param, 0, sizeof(struct ioctlparam));

		strncpy(param.toe_ip, bd_param[i].toe_ip, MAX_IP_LENGTH - 1);
		param.toe_port = 0xc00a + i + s5_tcp_port_base;
		param.volume_size = volume_size;
		param.volume_id = volume_id_base + i;
		param.snap_seq = 0;//latest snap sequence
	 	snprintf(param.dev_name, MAX_DEVICE_NAME_LEN, "s5bd%d", i);
		param.s5bd_id = i;
		s5bd_map_bd(&param);
	}
#endif
	return ret;
}

/**
 * \brief s5bd_mngt_dev_exit
 * exit function
 *
 * \return void
 */
static void __exit s5bd_mngt_dev_exit(void)
{
	int32_t i = 0;

#ifdef INTERNAL_DEBUG
	struct ioctlparam param;
	for (i = 0; i < disk_numbers; i ++)
	{
		memset(&param, 0, sizeof(struct ioctlparam));

		strncpy(param.toe_ip, bd_param[i].toe_ip, MAX_IP_LENGTH - 1);
		param.toe_port = 0xc00a + i + s5_tcp_port_base;
		param.volume_size = volume_size;
		param.volume_id = volume_id_base + i;
		param.snap_seq = 0;//latest snap sequence
	 	snprintf(param.dev_name, MAX_DEVICE_NAME_LEN, "s5bd%d", i);
		param.s5bd_id = i;
		if (s5bd_unmap_bd(&param))
			LOG_WARN("Faile to unmap volume(%s).", param.dev_name);
	}
#endif

	unregister_blkdev(s5bd_major, "s5bd");

	for(i = 0; i < MAX_DEVICE_NUM; ++i)
	{
		if (s5bd_mngt_dev[i])
		{
			LOG_ERROR("Device(%s) busy, please unmap all s5bd device. id(%d)",
				s5bd_mngt_dev[i]->dinfo.dev_name, i);
			return;
		}
	}

	if (misc_deregister(&s5bd_mngt_miscdev) < 0)
		LOG_ERROR("Failed to unmap device[%s].", s5bd_mngt_miscdev.name);
	else
		LOG_INFO("Succeed to unmap device[%s].", s5bd_mngt_miscdev.name);

	return;
}

module_init(s5bd_mngt_dev_init);
module_exit(s5bd_mngt_dev_exit);
MODULE_LICENSE("GPL");

