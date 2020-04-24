#include "special_vol_mgr.h"
#include <errno.h>

int pf_mount(const char *source
	, const char *target
	, const char *filesystemtype
	, unsigned long mountflags
	, const void *data)
{
	return mount(source, target, filesystemtype, mountflags, data);
}

int pf_remount(const char *source
	, const char *target
	, unsigned long mountflags
	, const void *data)
{
	return mount(source, target, NULL, MS_REMOUNT | mountflags, data);
}

int pf_umount(const char *target, int flag)
{
	if (flag)
	{
		return umount2(target, flag);
	}
	else
	{
		return umount(target);
	}
}

