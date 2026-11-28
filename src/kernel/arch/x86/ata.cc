#include <os.h>
#include <arch/x86/ata.h>
#include <arch/x86/storage_timeout.h>
#include <runtime/alloc.h>

DiskDevice *open_ahci_disk(u8 index, const char *name, bool publish);

namespace {

constexpr u8 ATA_REG_DATA = 0x00;
constexpr u8 ATA_REG_ERROR = 0x01;
constexpr u8 ATA_REG_SECCOUNT0 = 0x02;
constexpr u8 ATA_REG_LBA0 = 0x03;
constexpr u8 ATA_REG_LBA1 = 0x04;
constexpr u8 ATA_REG_LBA2 = 0x05;
constexpr u8 ATA_REG_HDDEVSEL = 0x06;
constexpr u8 ATA_REG_COMMAND = 0x07;
constexpr u8 ATA_REG_STATUS = 0x07;

constexpr u8 ATA_CMD_IDENTIFY = 0xEC;
constexpr u8 ATA_CMD_READ_SECTORS = 0x20;
constexpr u8 ATA_CMD_WRITE_SECTORS = 0x30;
constexpr u8 ATA_CMD_CACHE_FLUSH = 0xE7;

constexpr u8 ATA_SR_ERR = 0x01;
constexpr u8 ATA_SR_DRQ = 0x08;
constexpr u8 ATA_SR_DF = 0x20;
constexpr u8 ATA_SR_DRDY = 0x40;
constexpr u8 ATA_SR_BSY = 0x80;

constexpr u32 ATA_SECTOR_SIZE = 512;
constexpr u32 ATA_LBA_LIMIT = 1U << 28;

static void ata_io_wait(u16 ctrl_base) {
    for (u32 i = 0; i < 15; ++i) io.inb(ctrl_base);
}

static void serial_print_ata(const char* str) {
    while (*str) {
        asm volatile("outb %0, %1" : : "a"(*str), "Nd"((u16)0x3F8));
        str++;
    }
}

}

ATADevice::ATADevice(const char* name, u16 io_base, u16 ctrl_base, u8 slave, bool publish)
    : DiskDevice(name, publish), io_base_(io_base), ctrl_base_(ctrl_base), slave_(slave) {
    identify_.model[0] = '\0';
    identify_.total_sectors = 0;
    identify_.present = false;
}

ATADevice::~ATADevice() = default;

bool ATADevice::wait_status(u8 set, u8 clear, bool check_errors) {
    StorageTimeout timeout;
    do {
        u8 status = io.inb(ctrl_base_);
        if (!status || status == 0xff) return false;
        if (status & ATA_SR_BSY) continue;
        if (check_errors && (status & (ATA_SR_ERR | ATA_SR_DF))) return false;
        if ((status & set) == set && !(status & clear)) return true;
    } while (!timeout.expired());
    return false;
}

void ATADevice::select_drive(u32 lba) {
    u8 head = (lba >> 24) & 0x0F;
    io.outb(io_base_ + ATA_REG_HDDEVSEL, 0xE0 | (slave_ << 4) | head);
    ata_io_wait(ctrl_base_);
}

bool ATADevice::initialize() {
    IRQGuard guard;
    identify_.present = false;
    identify_.total_sectors = 0;
    io.outb(ctrl_base_, 2);
    select_drive(0);

    io.outb(io_base_ + ATA_REG_SECCOUNT0, 0);
    io.outb(io_base_ + ATA_REG_LBA0, 0);
    io.outb(io_base_ + ATA_REG_LBA1, 0);
    io.outb(io_base_ + ATA_REG_LBA2, 0);
    io.outb(io_base_ + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    ata_io_wait(ctrl_base_);

    u8 status = io.inb(io_base_ + ATA_REG_STATUS);
    if (status == 0 || status == 0xff) {
        serial_print_ata("[ATA] No device present on primary channel\n");
        return false;
    }

    if (!wait_status(0, 0, false) || io.inb(io_base_ + ATA_REG_LBA1) ||
        io.inb(io_base_ + ATA_REG_LBA2) || !wait_status(ATA_SR_DRQ, 0, true)) {
        serial_print_ata("[ATA] Device did not become ready\n");
        return false;
    }

    u16* buffer = (u16*)kmalloc(ATA_SECTOR_SIZE);
    if (!buffer)
        return false;

    for (u32 i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
        buffer[i] = io.inw(io_base_ + ATA_REG_DATA);
    }

    for (u32 i = 0; i < 20; ++i) {
        u16 word = buffer[27 + i];
        identify_.model[i * 2] = (char)((word >> 8) & 0xFF);
        identify_.model[i * 2 + 1] = (char)(word & 0xFF);
    }
    identify_.model[40] = '\0';

    for (int i = 39; i >= 0; --i) {
        if (identify_.model[i] == ' ' || identify_.model[i] == '\0') {
            identify_.model[i] = '\0';
        } else {
            break;
        }
    }

    identify_.total_sectors = ((u32)buffer[61] << 16) | buffer[60];
    if (identify_.total_sectors > ATA_LBA_LIMIT) identify_.total_sectors = ATA_LBA_LIMIT;
    identify_.present = (buffer[49] & (1U << 9)) && identify_.total_sectors;

    kfree(buffer);
    if (!identify_.present) return false;

    serial_print_ata(slave_ ? "[ATA] Primary slave detected: " : "[ATA] Primary master detected: ");
    serial_print_ata(identify_.model);
    serial_print_ata("\n");

    return true;
}

u32 ATADevice::read_blocks(u32 lba, u32 count, void* buffer) {
    IRQGuard guard;
    if (!buffer || !valid_range(lba, count))
        return ERROR_PARAM;

    u8* out = (u8*)buffer;
    for (u32 sector = 0; sector < count; ++sector) {
        select_drive(lba + sector);

        if (!wait_status(0, ATA_SR_DRQ, false))
            return RETURN_FAILURE;

        io.outb(io_base_ + ATA_REG_SECCOUNT0, 1);
        io.outb(io_base_ + ATA_REG_LBA0, (u8)((lba + sector) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA1, (u8)(((lba + sector) >> 8) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA2, (u8)(((lba + sector) >> 16) & 0xFF));
        io.outb(io_base_ + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
        ata_io_wait(ctrl_base_);

        if (!wait_status(ATA_SR_DRQ, 0, true))
            return RETURN_FAILURE;

        for (u32 i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
            u16 data = io.inw(io_base_ + ATA_REG_DATA);
            out[sector * ATA_SECTOR_SIZE + i * 2] = (u8)(data & 0xFF);
            out[sector * ATA_SECTOR_SIZE + i * 2 + 1] = (u8)((data >> 8) & 0xFF);
        }
        ata_io_wait(ctrl_base_);
        if (!wait_status(0, ATA_SR_DRQ, true))
            return RETURN_FAILURE;
    }

    return RETURN_OK;
}

u32 ATADevice::write_blocks(u32 lba, u32 count, const void* buffer) {
    IRQGuard guard;
    if (!buffer || !valid_range(lba, count))
        return ERROR_PARAM;

    const u8* in = (const u8*)buffer;
    
    for (u32 sector = 0; sector < count; ++sector) {
        select_drive(lba + sector);

        if (!wait_status(0, ATA_SR_DRQ, false))
            return RETURN_FAILURE;

        io.outb(io_base_ + ATA_REG_SECCOUNT0, 1);
        io.outb(io_base_ + ATA_REG_LBA0, (u8)((lba + sector) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA1, (u8)(((lba + sector) >> 8) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA2, (u8)(((lba + sector) >> 16) & 0xFF));
        io.outb(io_base_ + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
        ata_io_wait(ctrl_base_);

        if (!wait_status(ATA_SR_DRQ, 0, true))
            return RETURN_FAILURE;

        for (u32 i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
            u16 data = in[sector * ATA_SECTOR_SIZE + i * 2] |
                      (in[sector * ATA_SECTOR_SIZE + i * 2 + 1] << 8);
            io.outw(io_base_ + ATA_REG_DATA, data);
        }

        ata_io_wait(ctrl_base_);
        if (!wait_status(0, ATA_SR_DRQ, true))
            return RETURN_FAILURE;
    }

    io.outb(io_base_ + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_io_wait(ctrl_base_);
    if (!wait_status(0, ATA_SR_DRQ, true))
        return RETURN_FAILURE;

    return RETURN_OK;
}

DiskDevice *open_disk(u8 index, bool publish) {
    if (index > 1) return nullptr;
    const char *name = index ? "hdb" : "hda";
    if (DiskDevice *disk = open_ahci_disk(index, name, publish)) return disk;
    ATADevice *disk = new ATADevice(name, 0x1f0, 0x3f6, index, publish);
    if (disk && disk->initialize()) return disk;
    delete disk;
    return nullptr;
}
