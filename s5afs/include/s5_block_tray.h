#ifndef __S5_BLOCK_TRAY_H__
#define __S5_BLOCK_TRAY_H__

#include "s5_tray.h"

class BlockTray : public S5Tray {
    public:
        BlockTray();
        ~BlockTray();

        int init(const char *name) override;
        void destroy() override;
        int get_num_blocks(long *number) override;
        ssize_t sync_read(void *buffer, size_t size, __off_t offset) override;
        ssize_t sync_write(const void *buffer, size_t size, __off_t offset) override;
        ssize_t async_read(void *buffer, size_t size, __off_t offset, void *callback) override;
        ssize_t async_write(const void *buffer, size_t size, __off_t offset, void *callback) override;

    private:
        int fd;
};
#endif /* __S5_BLOCK_TRAY_H__ */