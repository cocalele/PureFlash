#ifndef __S5_TRAY_H__
#define __S5_TRAY_H__

#include <sys/types.h>

/* This a base class for device operations like pread, prwite, aios.
 * The purpose of this class is to separate OS calls from main store class.
 */

class PfTray {
    public:
        virtual ~PfTray() {}
        virtual int init(const char *name) = 0;
        virtual void destroy() = 0;
        virtual int get_num_blocks(long *number) = 0;
        virtual ssize_t sync_read(void *buffer, size_t size, __off_t offset) = 0;
        virtual ssize_t sync_write(const void *buffer, size_t size, __off_t offset) = 0;
        virtual ssize_t async_read(void *buffer, size_t size, __off_t offset, void *callback) = 0;
        virtual ssize_t async_write(const void *buffer, size_t size, __off_t offset, void *callback) = 0;
};

#endif /* __S5_DEVICE_H__ */
