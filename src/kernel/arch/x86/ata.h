#ifndef ATA_H
#define ATA_H

#include <arch/x86/disk.h>

struct ATAIdentifyData {
    char model[41];
    u32 total_sectors;
    bool present;
};

class ATADevice : public DiskDevice {
public:
    ATADevice(const char* name, u16 io_base, u16 ctrl_base, u8 slave, bool publish = true);
    virtual ~ATADevice();

    bool initialize();
    virtual u32 read_blocks(u32 lba, u32 count, void* buffer) override;
    virtual u32 write_blocks(u32 lba, u32 count, const void* buffer) override;

    u32 sector_count() const override { return identify_.present ? identify_.total_sectors : 0; }

private:
    bool wait_status(u8 set, u8 clear, bool check_errors);
    void select_drive(u32 lba);

    u16 io_base_;
    u16 ctrl_base_;
    u8 slave_;
    ATAIdentifyData identify_;
};

#endif
