#include <os.h>
#include <arch/x86/disk.h>
#include <arch/x86/serial.h>
#include <arch/x86/storage_timeout.h>
#include <arch/x86/vmm.h>

extern "C" {
    void *memcpy(void *, const void *, int);
    void *memset(void *, int, int);
}

namespace {

constexpr u32 pci_address = 0xcf8;
constexpr u32 pci_data = 0xcfc;
constexpr u16 pci_memory = 1U << 1;
constexpr u16 pci_bus_master = 1U << 2;
constexpr u16 pci_interrupt_disable = 1U << 10;
constexpr u32 ahci_class = 0x010601;
constexpr u32 register_window = 0xf0000000;
constexpr u32 ahci_enable = 1U << 31;
constexpr u32 interrupt_enable = 1U << 1;
constexpr u32 command_start = 1U << 0;
constexpr u32 fis_enable = 1U << 4;
constexpr u32 fis_running = 1U << 14;
constexpr u32 command_running = 1U << 15;
constexpr u32 task_busy = 0x88;
constexpr u32 task_error = 0x21;
constexpr u32 interrupt_errors = (1U << 30) | (1U << 29) | (1U << 28) | (1U << 27) | (1U << 24);
constexpr u32 sata_disk = 0x00000101;
constexpr u8 identify_device = 0xec;
constexpr u8 read_dma_ext = 0x25;
constexpr u8 write_dma_ext = 0x35;
constexpr u8 flush_cache = 0xe7;

struct Port {
    u32 list_base, list_upper, fis_base, fis_upper;
    u32 interrupt_status, interrupt_mask, command, reserved;
    u32 task_file, signature, sata_status, sata_control, sata_error, active, issue;
    u32 padding[17];
};

struct Controller {
    u32 capabilities, control, interrupt_status, ports_implemented;
    u32 version, ccc_control, ccc_ports, enclosure_location, enclosure_control;
    u32 capabilities2, handoff;
    u32 padding[53];
    Port ports[2];
};

struct CommandHeader {
    u16 flags, prdt_size;
    u32 transferred, table_base, table_upper;
    u32 reserved[4];
};

struct CommandTable {
    u8 fis[64];
    u8 atapi_and_reserved[64];
    u32 data_base, data_upper, reserved, byte_count;
};

struct DmaPage {
    CommandHeader commands[32];
    alignas(256) u8 received_fis[256];
    alignas(128) CommandTable table;
    alignas(2048) u8 data[2048];
};

static_assert(sizeof(Port) == 0x80 && __builtin_offsetof(Controller, ports) == 0x100, "AHCI registers");
static_assert(sizeof(CommandHeader) == 32 && __builtin_offsetof(CommandTable, data_base) == 0x80,
              "AHCI command layout");
static_assert(sizeof(DmaPage) == PAGESIZE && __builtin_offsetof(DmaPage, received_fis) == 0x400 &&
              __builtin_offsetof(DmaPage, table) == 0x500, "AHCI DMA layout");

static bool wait_clear(const volatile u32 &reg, u32 mask) {
    if (!(reg & mask)) return true;
    StorageTimeout timeout;
    do {
        if (!(reg & mask)) return true;
    } while (!timeout.expired());
    return false;
}

static volatile Controller *find_controller() {
    for (u32 function = 0; function < 256; ++function) {
        u32 config = 0x80000000U | (function << 8);
        io.outl(pci_address, config);
        if ((io.inl(pci_data) & 0xffff) == 0xffff) continue;
        io.outl(pci_address, config | 8);
        if (io.inl(pci_data) >> 8 != ahci_class) continue;
        io.outl(pci_address, config | 0x24);
        u32 bar = io.inl(pci_data);
        u32 base = bar & ~0x0fU;
        if (!base || (bar & 7) || (base & 0x3ff)) continue;
        u32 physical = base & ~(PAGESIZE - 1);
        if (vmm.map_page(kernel_directory, register_window, physical,
                         PG_PRESENT | PG_WRITE | PG_WRITE_THROUGH | PG_CACHE_DISABLE)) return nullptr;
        io.outl(pci_address, config | 4);
        io.outw(pci_data, io.inw(pci_data) | pci_memory | pci_bus_master | pci_interrupt_disable);
        auto *controller = reinterpret_cast<volatile Controller *>(register_window + base - physical);
        if (controller->capabilities2 & 1) {
            controller->handoff = (controller->handoff & ~(1U << 3)) | (1U << 1);
            if (!wait_clear(controller->handoff, (1U << 4) | 1U)) return nullptr;
        }
        controller->control = (controller->control | ahci_enable) & ~interrupt_enable;
        return controller;
    }
    return nullptr;
}

class AhciDisk : public DiskDevice {
public:
    AhciDisk(volatile Port &port, const char *name, bool publish)
        : DiskDevice(name, publish), port_(port) {}

    ~AhciDisk() override {
        IRQGuard guard;
        if (dma_ && stop()) vmm.free_frame((u32)dma_);
    }

    bool initialize() {
        if (!stop() || port_.issue || port_.active) return false;
        dma_ = (DmaPage *)vmm.alloc_frame();
        if (!dma_) return false;
        memset(dma_, 0, sizeof(*dma_));
        port_.interrupt_mask = 0;
        port_.list_base = (u32)dma_->commands;
        port_.list_upper = 0;
        port_.fis_base = (u32)dma_->received_fis;
        port_.fis_upper = 0;
        port_.sata_error = ~0U;
        port_.interrupt_status = ~0U;
        port_.command = port_.command | fis_enable;
        if (!wait_clear(port_.task_file, task_busy) || port_.signature != sata_disk) return false;
        port_.command = port_.command | command_start;
        if (!issue(identify_device, 0, 0, sector_size, false)) return false;
        u16 words[sector_size / sizeof(u16)];
        memcpy(words, dma_->data, sizeof(words));
        if ((words[49] & 0x300) != 0x300 || !(words[83] & (1U << 10))) return false;
        if ((words[106] & 0xd000) == 0x5000 && (words[117] != sector_size / 2 || words[118]))
            return false;
        sectors_ = (u32)words[100] | ((u32)words[101] << 16);
        if (words[102] || words[103]) sectors_ = ~0U;
        return sectors_ != 0;
    }

    u32 sector_count() const override { return sectors_; }

    u32 read_blocks(u32 lba, u32 count, void *buffer) override {
        return transfer(lba, count, nullptr, (u8 *)buffer);
    }

    u32 write_blocks(u32 lba, u32 count, const void *buffer) override {
        return transfer(lba, count, (const u8 *)buffer, nullptr);
    }

private:
    bool stop() {
        port_.command = port_.command & ~command_start;
        if (!wait_clear(port_.command, command_running)) return false;
        port_.command = port_.command & ~fis_enable;
        return wait_clear(port_.command, fis_running);
    }

    bool issue(u8 opcode, u32 lba, u16 sectors, u32 bytes, bool write) {
        if (failed_ || port_.issue || port_.active || !wait_clear(port_.task_file, task_busy))
            return false;
        CommandHeader &header = dma_->commands[0];
        CommandTable &table = dma_->table;
        header = {};
        header.flags = 5 | (write ? 1U << 6 : 0);
        header.prdt_size = bytes ? 1 : 0;
        header.table_base = (u32)&table;
        memset(&table, 0, sizeof(table));
        table.data_base = (u32)dma_->data;
        if (bytes) table.byte_count = bytes - 1;
        table.fis[0] = 0x27;
        table.fis[1] = 0x80;
        table.fis[2] = opcode;
        table.fis[4] = lba;
        table.fis[5] = lba >> 8;
        table.fis[6] = lba >> 16;
        if (sectors) table.fis[7] = 0x40;
        table.fis[8] = lba >> 24;
        table.fis[12] = sectors;
        table.fis[13] = sectors >> 8;
        port_.interrupt_status = ~0U;
        asm volatile("" ::: "memory");
        port_.issue = 1;
        if (port_.issue & 1) {
            StorageTimeout timeout;
            while (port_.issue & 1) {
                if ((port_.interrupt_status & interrupt_errors) || timeout.expired()) {
                    failed_ = true;
                    return false;
                }
            }
        }
        asm volatile("" ::: "memory");
        return !(port_.interrupt_status & interrupt_errors) && !(port_.task_file & task_error) &&
            header.transferred == bytes;
    }

    u32 transfer(u32 lba, u32 count, const u8 *source, u8 *destination) {
        IRQGuard guard;
        if ((!source && !destination) || !valid_range(lba, count)) return ERROR_PARAM;
        if (failed_) return RETURN_FAILURE;
        constexpr u32 max_sectors = sizeof(DmaPage::data) / sector_size;
        bool write = source != nullptr;
        while (count) {
            u32 sectors = count < max_sectors ? count : max_sectors;
            u32 bytes = sectors * sector_size;
            if (write) memcpy(dma_->data, source, bytes);
            if (!issue(write ? write_dma_ext : read_dma_ext, lba, sectors, bytes, write))
                return RETURN_FAILURE;
            if (write) source += bytes;
            else {
                memcpy(destination, dma_->data, bytes);
                destination += bytes;
            }
            lba += sectors;
            count -= sectors;
        }
        return !write || issue(flush_cache, 0, 0, 0, false) ? RETURN_OK : RETURN_FAILURE;
    }

    volatile Port &port_;
    DmaPage *dma_ = nullptr;
    u32 sectors_ = 0;
    bool failed_ = false;
};

}

DiskDevice *open_ahci_disk(u8 index, const char *name, bool publish) {
    IRQGuard guard;
    static bool probed = false;
    static volatile Controller *controller = nullptr;
    if (!probed) {
        probed = true;
        controller = find_controller();
    }
    if (!controller || index >= 2 || !(controller->ports_implemented & (1U << index))) return nullptr;
    volatile Port &port = controller->ports[index];
    if ((port.sata_status & 0x0f) != 3) return nullptr;
    AhciDisk *disk = new AhciDisk(port, name, publish);
    if (!disk || !disk->initialize()) {
        delete disk;
        return nullptr;
    }
    serial::write("[AHCI] ");
    serial::write(name);
    serial::write(" detected\n");
    return disk;
}
