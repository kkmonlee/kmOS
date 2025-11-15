#ifndef ATA_H
#define ATA_H

#include <core/block_device.h>

struct ATAIdentifyData {
    char model[41];
    u32 total_sectors;
    bool present;
};

class ATADevice : public BlockDevice {
public:
    ATADevice(const char* name, u16 io_base, u16 ctrl_base, u8 slave);
    virtual ~ATADevice();

    bool initialize();
    virtual u32 read_blocks(u32 lba, u32 count, void* buffer) override;
    virtual u32 write_blocks(u32 lba, u32 count, const void* buffer) override;

    const ATAIdentifyData& identify() const { return identify_; }

private:
    bool wait_busy();
    bool wait_data_ready();
    void select_drive(u32 lba);

    u16 io_base_;
    u16 ctrl_base_;
    u8 slave_;
    ATAIdentifyData identify_;
};

void ata_init();
ATADevice* ata_primary_master();

#endif
