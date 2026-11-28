#ifndef X86_DISK_H
#define X86_DISK_H

#include <core/block_device.h>

class DiskDevice : public BlockDevice {
public:
    static constexpr u32 sector_size = 512;

    DiskDevice(const char *name, bool publish) : BlockDevice(name, sector_size, publish) {}
    virtual u32 sector_count() const = 0;

protected:
    bool valid_range(u32 lba, u32 count) const {
        u32 sectors = sector_count();
        return count && lba < sectors && count <= sectors - lba && count <= 0xffffffffU / sector_size;
    }
};

DiskDevice *open_disk(u8 index, bool publish = false);

#endif
