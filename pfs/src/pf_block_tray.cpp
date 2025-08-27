/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#include <sys/stat.h>
#include <unistd.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "pf_block_tray.h"
#include "pf_log.h"

BlockTray::BlockTray()
{
}

BlockTray::~BlockTray()
{
}

int BlockTray::init(const char *name)
{
        fd = open(name, O_RDWR|O_DIRECT);
        return fd;
}

void BlockTray::destroy()
{
        close(fd);
}

int BlockTray::get_num_blocks(long *number)
{
  struct stat fst;
  int rc = fstat(fd, &fst);
  if(rc != 0)
  {
    rc = -errno;
    S5LOG_ERROR("Failed fstat, rc:%d", rc);
    return rc;
  }
  if(S_ISBLK(fst.st_mode ))
    return ioctl(fd, BLKGETSIZE, number);
  else
  {
    *number = fst.st_size/512;
  }
  return 0;
}

ssize_t BlockTray::sync_read(void *buffer, size_t size, __off_t offset)
{
        return pread(fd, buffer, size, offset);
}

ssize_t BlockTray::sync_write(const void *buffer, size_t size, __off_t offset)
{
        return pwrite(fd, buffer, size, offset);
}

ssize_t BlockTray::async_read(void *buffer, size_t size, __off_t offset, void *callback)
{
        return 0;
}

ssize_t BlockTray::async_write(const void *buffer, size_t size, __off_t offset, void *callback)
{
        return 0;
}
