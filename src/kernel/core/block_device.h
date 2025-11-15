#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <core/device.h>

class BlockDevice : public Device {
public:
    explicit BlockDevice(const char* name, u32 block_size = 512);
    virtual ~BlockDevice();

    virtual u32 read_blocks(u32 lba, u32 count, void* buffer) = 0;
    virtual u32 write_blocks(u32 lba, u32 count, const void* buffer) = 0;

    u32 get_block_size() const { return block_size_; }
    virtual u32 read(u32 pos, u8* buffer, u32 size) override;
    virtual u32 write(u32 pos, u8* buffer, u32 size) override;

protected:
    u32 block_size_;
};

#endif
