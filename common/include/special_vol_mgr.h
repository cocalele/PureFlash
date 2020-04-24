/**
 * Copyright (C), 2015-.
 * @file
 * Special Volume Management API
 *
 * use to implement func to manage the special volumes(eg. mount/umount)
 * 
 *
 * @author yuanxin
 */

#ifndef _SPECIAL_VOL_MGR
#define _SPECIAL_VOL_MGR

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/mount.h>

#define S5_META_DEV	"/dev/md/s5meta"
#define S5_ETM_DEV	"/dev/md/s5etm"
#define S5_ETS_DEV	"/dev/md/s5ets"

#define S5_META_MOUNT_DIR	"/opt/s5sp/s5meta"
#define S5_ETM_MOUNT_DIR	"/opt/s5sp/s5etm"
#define S5_ETS_MOUNT_DIR	"/opt/s5sp/s5tes"

/**mount flag rw*/
#define S5_MOUNT_FLAG_RW	0
/**mount flag ro*/
#define S5_MOUNT_FLAG_RO	MS_RDONLY

/**remount flag rw*/
#define S5_REMOUNT_FLAG_RW	~MS_RDONLY
/**mount flag ro*/
#define S5_REMOUNT_FLAG_RO	MS_RDONLY

#ifndef NULL
#define NULL ((void*)0)
#endif

/**
 * mount source(device) to target(dir)
 *
 * call system C-API(mount) to mount a block device to a dir, use cmd 'man 2 mount' to know more
 *
 * @param[in]	source	a file path to a block device(mkfs firstly)
 * @param[in]	target	a dir path
 * @param[in]	filesystemtype	file system type, ignore it, use NULL at present
 * @param[in]	mountflagsfile  mount flags, if mount to rw use macro S5_MOUNT_FLAG_RW
 * 					     if mount to ro use macro S5_MOUNT_FLAG_RO
 * 					     else use cmd 'man 2 mount' to know more
 * @param[in]	data const char*, ignore it, use NULL at present
 * @return 0 on success and negative for errors, for detail error in errno,and see more use cmd man 2 mount
 */
int pf_mount(const char *source
	, const char *target
	, const char *filesystemtype
	, unsigned long mountflags
	, const void *data);

/**
 * modify mount options between target(dir) and system(device), eg. modify read-write permission to read-only
 *
 * call system C-API(mount) to remount a block device to a dir, use cmd 'man 2 mount' to know more
 *
 * @param[in]	source	a file path to a block device(mkfs firstly)
 * @param[in]	target	a dir path
 * @param[in]	mountflagsfile  mount flags, if modify to rw use macro S5_REMOUNT_FLAG_RW
 * 					     if modify to ro use macro S5_REMOUNT_FLAG_RO
 * 					     else use cmd 'man 2 mount' to know more
 * @param[in]	data const char*, ignore it, use NULL at present
 * @return 0 on success and negative for errors, for detail error in errno,and see more use cmd man 2 mount
 */
int pf_remount(const char *source
	, const char *target
	, unsigned long mountflags
	, const void *data);

/**
 * umount target(dir) from a system(device)
 *
 * call system C-API(umount/umount2) to umount a dir, use cmd 'man 2 umount' to know more
 *
 * @param[in]	target	a dir path
 * @param[in]	flag	ignore it, use 0 at present
 * @return 0 on success and negative for errors, for detail error in errno,and see more use cmd man 2 umount
 */
int pf_umount(const char *target, int flag);

/*
int pf_mount_meta();
int pf_mount_etm();
int pf_mount_ets();

int pf_remount_meta();
int pf_remount_etm();
int pf_remount_ets();
*/

#ifdef __cplusplus
}
#endif

#endif

