#include "s5_block_tray.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

BlockTray::BlockTray()
{
}

BlockTray::~BlockTray()
{
}

int BlockTray::init(const char *name)
{
        fd = open(name, O_RDWR, O_DIRECT);
        return fd;
}

void BlockTray::destroy()
{
        close(fd);
}

int BlockTray::get_num_blocks(long *number)
{
        return ioctl(fd, BLKGETSIZE, number);
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
