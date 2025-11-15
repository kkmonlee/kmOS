#include <os.h>
#include <arch/x86/ata.h>
#include <arch/x86/io.h>
#include <runtime/alloc.h>

extern IO io;

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
constexpr u8 ATA_REG_CONTROL = 0x02;
constexpr u8 ATA_REG_ALTSTATUS = 0x02;

constexpr u8 ATA_CMD_IDENTIFY = 0xEC;
constexpr u8 ATA_CMD_READ_SECTORS = 0x20;

constexpr u8 ATA_SR_ERR = 0x01;
constexpr u8 ATA_SR_DRQ = 0x08;
constexpr u8 ATA_SR_DF = 0x20;
constexpr u8 ATA_SR_DRDY = 0x40;
constexpr u8 ATA_SR_BSY = 0x80;

constexpr u32 ATA_SECTOR_SIZE = 512;

static void ata_io_wait(u16 ctrl_base) {
    io.inb(ctrl_base + ATA_REG_ALTSTATUS);
    io.inb(ctrl_base + ATA_REG_ALTSTATUS);
    io.inb(ctrl_base + ATA_REG_ALTSTATUS);
    io.inb(ctrl_base + ATA_REG_ALTSTATUS);
}

static void serial_print_ata(const char* str) {
    while (*str) {
        asm volatile("outb %0, %1" : : "a"(*str), "Nd"((u16)0x3F8));
        str++;
    }
}

}

ATADevice::ATADevice(const char* name, u16 io_base, u16 ctrl_base, u8 slave)
    : BlockDevice(name, ATA_SECTOR_SIZE), io_base_(io_base), ctrl_base_(ctrl_base), slave_(slave) {
    identify_.model[0] = '\0';
    identify_.total_sectors = 0;
    identify_.present = false;
}

ATADevice::~ATADevice() = default;

bool ATADevice::wait_busy() {
    for (u32 i = 0; i < 100000; ++i) {
        u8 status = io.inb(io_base_ + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
    }
    return false;
}

bool ATADevice::wait_data_ready() {
    for (u32 i = 0; i < 100000; ++i) {
        u8 status = io.inb(io_base_ + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return true;
        }
        if (status & ATA_SR_ERR || status & ATA_SR_DF) {
            return false;
        }
    }
    return false;
}

void ATADevice::select_drive(u32 lba) {
    u8 head = (lba >> 24) & 0x0F;
    io.outb(io_base_ + ATA_REG_HDDEVSEL, 0xE0 | (slave_ << 4) | head);
    ata_io_wait(ctrl_base_);
}

bool ATADevice::initialize() {
    select_drive(0);

    io.outb(io_base_ + ATA_REG_SECCOUNT0, 0);
    io.outb(io_base_ + ATA_REG_LBA0, 0);
    io.outb(io_base_ + ATA_REG_LBA1, 0);
    io.outb(io_base_ + ATA_REG_LBA2, 0);
    io.outb(io_base_ + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    ata_io_wait(ctrl_base_);

    u8 status = io.inb(io_base_ + ATA_REG_STATUS);
    if (status == 0) {
        serial_print_ata("[ATA] No device present on primary channel\n");
        return false;
    }

    if (!wait_data_ready()) {
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
    identify_.present = true;

    kfree(buffer);

    serial_print_ata("[ATA] Primary master detected: ");
    serial_print_ata(identify_.model);
    serial_print_ata("\n");

    return true;
}

u32 ATADevice::read_blocks(u32 lba, u32 count, void* buffer) {
    if (!identify_.present || !buffer || count == 0)
        return ERROR_PARAM;

    u8* out = (u8*)buffer;
    for (u32 sector = 0; sector < count; ++sector) {
        select_drive(lba + sector);

        if (!wait_busy())
            return RETURN_FAILURE;

        io.outb(io_base_ + ATA_REG_SECCOUNT0, 1);
        io.outb(io_base_ + ATA_REG_LBA0, (u8)((lba + sector) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA1, (u8)(((lba + sector) >> 8) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA2, (u8)(((lba + sector) >> 16) & 0xFF));
        io.outb(io_base_ + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

        if (!wait_data_ready())
            return RETURN_FAILURE;

        for (u32 i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
            u16 data = io.inw(io_base_ + ATA_REG_DATA);
            out[sector * ATA_SECTOR_SIZE + i * 2] = (u8)(data & 0xFF);
            out[sector * ATA_SECTOR_SIZE + i * 2 + 1] = (u8)((data >> 8) & 0xFF);
        }
    }

    return RETURN_OK;
}

u32 ATADevice::write_blocks(u32 lba, u32 count, const void* buffer) {
    if (!identify_.present || !buffer || count == 0)
        return ERROR_PARAM;

    const u8* in = (const u8*)buffer;
    
    for (u32 sector = 0; sector < count; ++sector) {
        select_drive(lba + sector);

        if (!wait_busy())
            return RETURN_FAILURE;

        io.outb(io_base_ + ATA_REG_SECCOUNT0, 1);
        io.outb(io_base_ + ATA_REG_LBA0, (u8)((lba + sector) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA1, (u8)(((lba + sector) >> 8) & 0xFF));
        io.outb(io_base_ + ATA_REG_LBA2, (u8)(((lba + sector) >> 16) & 0xFF));
        io.outb(io_base_ + ATA_REG_COMMAND, 0x30);

        if (!wait_data_ready())
            return RETURN_FAILURE;

        for (u32 i = 0; i < ATA_SECTOR_SIZE / 2; ++i) {
            u16 data = in[sector * ATA_SECTOR_SIZE + i * 2] |
                      (in[sector * ATA_SECTOR_SIZE + i * 2 + 1] << 8);
            io.outw(io_base_ + ATA_REG_DATA, data);
        }

        if (!wait_busy())
            return RETURN_FAILURE;

        u8 status = io.inb(io_base_ + ATA_REG_STATUS);
        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return RETURN_FAILURE;
    }

    io.outb(io_base_ + ATA_REG_COMMAND, 0xE7);
    wait_busy();

    return RETURN_OK;
}

static ATADevice* g_primary_master = nullptr;

ATADevice* ata_primary_master() {
    return g_primary_master;
}

void ata_init() {
    if (g_primary_master) {
        return;
    }

    ATADevice* dev = new ATADevice("hda", 0x1F0, 0x3F6, 0);
    if (dev && dev->initialize()) {
        g_primary_master = dev;
    } else {
        if (dev) {
            delete dev;
        }
        serial_print_ata("[ATA] Primary master initialization failed\n");
    }
}
