/* s5ioctl.h - xxxx */

/*
 * Copyright (c) 2015 NetBric Systems, Inc.
 *
 * The right to copy, distribute, modify or otherwise make use
 * of this software may be licensed only pursuant to the terms
 * of an applicable NetBric license agreement.
 */


#ifndef __S5IOCTL_H__
#define __S5IOCTL_H__

/* misc device name*/
#define DEVICENAME "s5bd"

#ifndef MAX_IP_LENGTH
#define MAX_IP_LENGTH 16
#endif

//hard code in genhd.c:L246 kernel version:3.10
#ifndef MAX_DEVICE_NAME_LEN
#define MAX_DEVICE_NAME_LEN 16
#endif

#ifndef MAX_DEVICE_NUM
#define MAX_DEVICE_NUM 64
#endif
#define MAX_CONDUCTOR_CNT 2

#include <linux/limits.h>
#include "bdd_message.h"

#define BDDBIN		"/var/tmp/bdd.bin" //change directory to /etc/s5 later
#define BDDBINBAK	"/var/tmp/.bdd.bin.bak" //change directory to /etc/s5 later

struct ioctlparam;

/**
 *define ioctl codes for interfacing between kernel_module and user program
 */
#define S5BDMONITOR_CODE 0xcc

#define MAP_DEVICE _IOWR (S5BDMONITOR_CODE, 0x0, struct ioctlparam)
#define UNMAP_DEVICE _IOWR (S5BDMONITOR_CODE, 0x1, struct ioctlparam)
#define LIST_DEVICE _IOWR (S5BDMONITOR_CODE, 0x2, struct ioctlparam)

#define BDEVICE_INACTIVE	0
#define BDEVICE_ACTIVE		1

typedef struct bdevice
{
    /* mgt_device_ctx index, same as s5bd%d index */

    int bddev_id;

	/* get from s5bd command tool */

    struct device_info dinfo;
} bdevice_t;

typedef struct ioctlparam
{
	struct bdevice bdev;

    /* ioctrl return value */
    int retval;

} __attribute__((packed)) ioctlparam_t;

#endif	/* __S5IOCTL_H__ */

