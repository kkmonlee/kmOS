#include <os.h>
#include <swap.h>
#include <disk.h>
#include <architecture.h>
#include <serial.h>
#include <vmm.h>
#include <core/boot.h>
#include <core/process.h>

extern "C" int strcmp(const char *left, const char *right);

static constexpr char signature[] = "KMOS-SWAP";
static constexpr u32 slots_per_device = 1U << 16;
static constexpr u32 max_devices = 16;

SwapManager swap_manager;

class SwapDiskFile : public File {
public:
    SwapDiskFile() : File("hdb", TYPE_FILE) {}
    ~SwapDiskFile() override { delete disk; }

    bool initialize() {
        disk = open_disk(1);
        if (!disk) return false;
        u32 pages = disk->sector_count() / (PAGESIZE / DiskDevice::sector_size);
        if (pages < 2 || pages > slots_per_device + 1) return false;
        setSize(pages * PAGESIZE);
        return true;
    }

    u32 open(u32) override { return 0; }
    u32 close() override { return 0; }

    u32 read(u32 pos, u8 *buffer, u32 count) override {
        if (!valid_range(pos, count)) return (u32)-22;
        return disk->read_blocks(pos / DiskDevice::sector_size, count / DiskDevice::sector_size, buffer)
            == RETURN_OK ? count : (u32)-5;
    }

    u32 write(u32 pos, u8 *buffer, u32 count) override {
        if (!valid_range(pos, count)) return (u32)-22;
        return disk->write_blocks(pos / DiskDevice::sector_size, count / DiskDevice::sector_size, buffer)
            == RETURN_OK ? count : (u32)-5;
    }

private:
    bool valid_range(u32 pos, u32 count) {
        return !(pos % DiskDevice::sector_size) && !(count % DiskDevice::sector_size) &&
            pos < getSize() && count <= getSize() - pos;
    }

    DiskDevice *disk = nullptr;
};

void init_swap(const multiboot_info *boot) {
    if (!(boot->flags & 4) || !boot->cmdline || boot->cmdline > USER_OFFSET - PAGESIZE ||
        !boot_has_argument((const char *)boot->cmdline, "swap=hdb")) return;
    SwapDiskFile *file = new SwapDiskFile();
    if (file && file->initialize() && swap_manager.add_file(file)) {
        serial::write("[SWAP] hdb enabled\n");
        return;
    }
    delete file;
    serial::write("[SWAP] hdb unavailable or invalid\n");
}

bool SwapManager::add_file(File *file) {
    IRQGuard guard;
    if (!file || file->getType() != TYPE_FILE || !file->getName() || file->getParent() ||
        file->getSize() % PAGESIZE || file->getSize() < 2 * PAGESIZE) return false;
    u32 pages = file->getSize() / PAGESIZE - 1;
    if (pages > slots_per_device) return false;
    u32 ids = 0;
    for (swap_device *dev = devices; dev; dev = dev->next) {
        if (!strcmp(dev->file->getName(), file->getName())) return false;
        ids |= 1U << dev->id;
    }
    if (ids == (1U << max_devices) - 1) return false;
    if (file->open(2)) return false;

    u8 header[512];
    bool valid = file->read(0, header, sizeof(header)) == sizeof(header);
    for (u32 i = 0; valid && i < sizeof(signature); ++i)
        valid = header[i] == signature[i];
    if (!valid) {
        file->close();
        return false;
    }

    u16 *references = (u16 *)kmalloc(pages * sizeof(u16));
    swap_device *dev = (swap_device *)kmalloc(sizeof(swap_device));
    if (!dev || !references) {
        kfree(references);
        kfree(dev);
        file->close();
        return false;
    }
    for (u32 i = 0; i < pages; ++i) references[i] = 0;
    u32 id = 0;
    while (ids & (1U << id)) ++id;
    *dev = {file, id, pages, 0, 0, references, devices};
    devices = dev;
    return true;
}

bool SwapManager::remove_file(const char *name) {
    IRQGuard guard;
    swap_device **link = &devices;
    while (*link && strcmp((*link)->file->getName(), name)) link = &(*link)->next;
    swap_device *dev = *link;
    if (!dev || dev->inuse_pages) return false;
    *link = dev->next;
    dev->file->close();
    delete dev->file;
    kfree(dev->references);
    kfree(dev);
    return true;
}

swap_device *SwapManager::find_device(u32 slot) {
    for (swap_device *dev = devices; dev; dev = dev->next) {
        if (dev->id == slot / slots_per_device &&
            (slot % slots_per_device) < dev->pages) return dev;
    }
    return nullptr;
}

bool SwapManager::retain_slot(u32 slot) {
    IRQGuard guard;
    swap_device *dev = find_device(slot);
    if (!dev) return false;
    u16 &references = dev->references[slot % slots_per_device];
    if (!references || references == 0xffff) return false;
    ++references;
    return true;
}

void SwapManager::release_slot(u32 slot) {
    IRQGuard guard;
    swap_device *dev = find_device(slot);
    if (!dev) return;
    u16 &references = dev->references[slot % slots_per_device];
    if (references && !--references) --dev->inuse_pages;
}

bool SwapManager::swap_out_page(page_directory *pd, u32 address) {
    IRQGuard guard;
    if (!pd || pd == kernel_directory || address < USER_OFFSET || address >= USER_STACK ||
        (address & (PAGESIZE - 1))) return false;
    page_table_entry *table = vmm.get_page_table(pd, address, 0);
    const page_directory_entry &directory = pd->tables[VADDR_PD_OFFSET(address)];
    if (!table || !directory.user || directory.page_size) return false;
    page_table_entry &entry = table[VADDR_PT_OFFSET(address)];
    if (!entry.present || !entry.user) return false;
    u32 frame = entry.frame << 12;
    if (vmm.frame_references(frame) != 1) return false;

    for (swap_device *dev = devices; dev; dev = dev->next) {
        if (dev->inuse_pages == dev->pages) continue;
        constexpr u32 max_write_attempts = 4;
        u32 index = dev->pages;
        u32 attempts = 0;
        for (u32 scanned = 0; scanned < dev->pages && attempts < max_write_attempts; ++scanned) {
            u32 candidate = dev->next_slot;
            dev->next_slot = (candidate + 1) % dev->pages;
            if (dev->references[candidate]) continue;
            ++attempts;
            if (dev->file->write((candidate + 1) * PAGESIZE, (u8 *)frame, PAGESIZE) == PAGESIZE) {
                index = candidate;
                break;
            }
        }
        if (index == dev->pages) continue;
        dev->references[index] = 1;
        ++dev->inuse_pages;
        bool writable = entry.writable || entry.cow;
        entry = {};
        entry.writable = writable;
        entry.user = 1;
        entry.swapped = 1;
        entry.frame = dev->id * slots_per_device + index;
        vmm.invalidate_page(pd, address);
        vmm.free_frame(frame);
        return true;
    }
    return false;
}

int SwapManager::swap_in_page(page_directory *pd, u32 address, u32 start, u32 end) {
    IRQGuard guard;
    page_table_entry *table = vmm.get_page_table(pd, address, 0);
    if (!table || !pd->tables[VADDR_PD_OFFSET(address)].user) return -14;
    page_table_entry &entry = table[VADDR_PT_OFFSET(address)];
    if (entry.present || !entry.swapped || !entry.user) return -14;
    u32 slot = entry.frame;
    swap_device *dev = find_device(slot);
    u32 index = slot % slots_per_device;
    if (!dev || !dev->references[index]) return -5;
    u32 frame = vmm.alloc_user_frame(pd, start, end);
    if (!frame) return -12;
    if (dev->file->read((index + 1) * PAGESIZE, (u8 *)frame, PAGESIZE) != PAGESIZE) {
        vmm.free_frame(frame);
        return -5;
    }
    entry.frame = frame >> 12;
    entry.swapped = 0;
    entry.present = 1;
    vmm.invalidate_page(pd, address);
    release_slot(slot);
    return 0;
}

bool SwapManager::reclaim_from(page_directory *pd, u32 start, u32 end, bool cold_only) {
    if (!pd || pd == kernel_directory) return false;
    for (u32 i = VADDR_PD_OFFSET(USER_OFFSET); i < VADDR_PD_OFFSET(USER_STACK); ++i) {
        if (!pd->tables[i].present || !pd->tables[i].user || pd->tables[i].page_size) continue;
        page_table_entry *table = pd->page_tables[i];
        for (u32 j = 0; j < 1024; ++j) {
            page_table_entry &entry = table[j];
            if (!entry.present || !entry.user || vmm.frame_references(entry.frame << 12) != 1)
                continue;
            u32 address = (i << 22) | (j << 12);
            if (address >= start && address < end) continue;
            if (cold_only && entry.accessed) {
                entry.accessed = 0;
                vmm.invalidate_page(pd, address);
                continue;
            }
            return swap_out_page(pd, address);
        }
    }
    return false;
}

bool SwapManager::reclaim_page(page_directory *protected_pd, u32 start, u32 end) {
    IRQGuard guard;
    swap_device *dev = devices;
    while (dev && dev->inuse_pages == dev->pages) dev = dev->next;
    if (!dev) return false;

    for (bool cold_only = true;; cold_only = false) {
        Process *process = arch.plist;
        if (process) do {
            page_directory *pd = process->getPInfo()->pd;
            if (pd != current_directory && process->getState() != ZOMBIE &&
                reclaim_from(pd, pd == protected_pd ? start : 0,
                             pd == protected_pd ? end : 0, cold_only)) return true;
            process = process->getPNext();
        } while (process && process != arch.plist);
        if (reclaim_from(current_directory, current_directory == protected_pd ? start : 0,
                         current_directory == protected_pd ? end : 0, cold_only)) return true;
        if (!cold_only) return false;
    }
}
