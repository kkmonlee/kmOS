#include <os.h>
#include <swap.h>
#include <vmm.h>
#include <runtime/alloc.h>

extern "C" {
    int strlen(const char *s);
    int strcmp(const char *dst, const char *src);
    int strcpy(char *dst, const char *src);
}

SwapManager swap_manager;

static int swap_file_read_page(struct swap_device *dev, u32 offset, void *buffer);
static int swap_file_write_page(struct swap_device *dev, u32 offset, void *buffer);
static int swap_file_activate(struct swap_device *dev);
static int swap_file_deactivate(struct swap_device *dev);

void SwapManager::init() {
    io.print("[SWAP] Initializing swap manager\n");
    
    swap_devices = nullptr;
    total_swap_pages = 0;
    used_swap_pages = 0;
    
    lru_head = nullptr;
    lru_tail = nullptr;
    lru_count = 0;
    access_counter = 0;
    
    swap_in_count = 0;
    swap_out_count = 0;
    reclaim_attempts = 0;
    
    io.print("[SWAP] Swap manager initialized\n");
}

int SwapManager::add_swap_device(const char *path, u32 type, u32 priority) {
    if (find_swap_device(path)) {
        io.print("[SWAP] Device %s already exists\n", path);
        return -1;
    }
    
    struct swap_device *dev = (struct swap_device *)kmalloc(sizeof(struct swap_device));
    if (!dev) {
        io.print("[SWAP] Failed to allocate swap device\n");
        return -1;
    }
    
    dev->type = type;
    dev->flags = 0;
    dev->pages = 0;
    dev->inuse_pages = 0;
    dev->prio = priority;
    dev->path = (char *)kmalloc(strlen(path) + 1);
    if (!dev->path) {
        kfree(dev);
        return -1;
    }
    strcpy(dev->path, path);
    
    dev->bitmap = nullptr;
    dev->bitmap_size = 0;
    
    switch (type) {
        case SWAP_TYPE_FILE:
            dev->read_page = swap_file_read_page;
            dev->write_page = swap_file_write_page;
            dev->activate = swap_file_activate;
            dev->deactivate = swap_file_deactivate;
            break;
        
        default:
            kfree(dev->path);
            kfree(dev);
            return -1;
    }
    
    if (dev->activate(dev) != 0) {
        io.print("[SWAP] Failed to activate swap device %s\n", path);
        kfree(dev->path);
        kfree(dev);
        return -1;
    }
    
    dev->next = swap_devices;
    swap_devices = dev;
    total_swap_pages += dev->pages;
    
    io.print("[SWAP] Added swap device %s with %d pages (priority %d)\n", 
             path, dev->pages, priority);
    
    return 0;
}

int SwapManager::remove_swap_device(const char *path) {
    struct swap_device *dev = find_swap_device(path);
    if (!dev) {
        io.print("[SWAP] Device %s not found\n", path);
        return -1;
    }
    
    if (dev->inuse_pages > 0) {
        io.print("[SWAP] Cannot remove active swap device %s\n", path);
        return -1;
    }
    
    if (dev->deactivate(dev) != 0) {
        io.print("[SWAP] Failed to deactivate swap device %s\n", path);
        return -1;
    }
    
    if (swap_devices == dev) {
        swap_devices = dev->next;
    } else {
        struct swap_device *prev = swap_devices;
        while (prev->next != dev) {
            prev = prev->next;
        }
        prev->next = dev->next;
    }
    
    total_swap_pages -= dev->pages;
    
    kfree(dev->bitmap);
    kfree(dev->path);
    kfree(dev);
    
    io.print("[SWAP] Removed swap device %s\n", path);
    return 0;
}

u32 SwapManager::swap_out_page(u32 virtual_addr) {
    struct swap_device *dev;
    u32 offset;
    
    if (allocate_swap_entry(&dev, &offset) != 0) {
        io.print("[SWAP] No swap space available for page %x\n", virtual_addr);
        return 0;
    }
    
    u32 physical_addr = vmm.get_physical_addr(current_directory, virtual_addr);
    if (physical_addr == 0) {
        io.print("[SWAP] Page %x not mapped\n", virtual_addr);
        return 0;
    }
    
    if (dev->write_page(dev, offset, (void *)physical_addr) != 0) {
        io.print("[SWAP] Failed to write page %x to swap\n", virtual_addr);
        free_swap_entry(((dev - swap_devices) << 24) | offset);
        return 0;
    }
    
    u32 swap_entry_id = ((u32)(dev - swap_devices) << 24) | offset;
    vmm.add_swapped_page(current_directory, virtual_addr, swap_entry_id);
    vmm.unmap_page(current_directory, virtual_addr);
    remove_from_lru(virtual_addr);
    
    swap_out_count++;
    used_swap_pages++;
    dev->inuse_pages++;
    
    return swap_entry_id;
}

int SwapManager::swap_in_page(u32 virtual_addr, u32 swap_entry) {
    u32 dev_id = (swap_entry >> 24) & 0xFF;
    u32 offset = swap_entry & 0xFFFFFF;
    
    struct swap_device *dev = swap_devices;
    for (u32 i = 0; i < dev_id && dev; i++) {
        dev = dev->next;
    }
    
    if (!dev) {
        io.print("[SWAP] Invalid swap device for entry %x\n", swap_entry);
        return -1;
    }
    
    u32 frame = vmm.alloc_frame();
    if (frame == 0) {
        io.print("[SWAP] No memory available for swap-in\n");
        return -1;
    }
    
    if (dev->read_page(dev, offset, (void *)frame) != 0) {
        io.print("[SWAP] Failed to read page from swap\n");
        vmm.free_frame(frame);
        return -1;
    }
    
    if (vmm.map_page(current_directory, virtual_addr, frame, PG_PRESENT | PG_WRITE | PG_USER) != 0) {
        io.print("[SWAP] Failed to map swapped-in page\n");
        vmm.free_frame(frame);
        return -1;
    }
    
    free_swap_entry(swap_entry);
    add_to_lru(virtual_addr);
    
    swap_in_count++;
    used_swap_pages--;
    dev->inuse_pages--;
    
    return 0;
}

u32 SwapManager::check_memory_pressure() {
    struct memory_stats stats;
    get_memory_stats(&stats);
    
    if (stats.free_pages == 0) return MEMORY_PRESSURE_CRITICAL;
    
    u32 usage_percent = (stats.used_pages * 100) / stats.total_pages;
    
    if (usage_percent >= SWAP_THRESHOLD_CRITICAL) return MEMORY_PRESSURE_CRITICAL;
    if (usage_percent >= SWAP_THRESHOLD_HIGH) return MEMORY_PRESSURE_HIGH;
    if (usage_percent >= SWAP_THRESHOLD_MEDIUM) return MEMORY_PRESSURE_MEDIUM;
    if (usage_percent >= SWAP_THRESHOLD_LOW) return MEMORY_PRESSURE_LOW;
    
    return MEMORY_PRESSURE_NONE;
}

int SwapManager::reclaim_pages(u32 target_pages) {
    u32 reclaimed = 0;
    reclaim_attempts++;
    
    while (reclaimed < target_pages && lru_count > 0) {
        struct page_lru *victim = find_victim_page();
        if (!victim) break;
        
        u32 swap_entry = swap_out_page(victim->virtual_addr);
        if (swap_entry != 0) {
            reclaimed++;
        } else {
            break;
        }
    }
    
    io.print("[SWAP] Reclaimed %d pages (target: %d)\n", reclaimed, target_pages);
    return reclaimed;
}

struct page_lru *SwapManager::find_victim_page() {
    if (!lru_tail) return nullptr;
    
    struct page_lru *victim = lru_tail;
    
    while (victim) {
        if (!(victim->flags & SWAP_FLAG_LOCKED)) {
            return victim;
        }
        victim = victim->prev;
    }
    
    return lru_tail;
}

void SwapManager::update_page_access(u32 virtual_addr) {
    struct page_lru *page = lru_head;
    
    while (page) {
        if (page->virtual_addr == virtual_addr) {
            page->access_time = ++access_counter;
            
            if (page != lru_head) {
                if (page->prev) page->prev->next = page->next;
                if (page->next) page->next->prev = page->prev;
                if (page == lru_tail) lru_tail = page->prev;
                
                page->prev = nullptr;
                page->next = lru_head;
                if (lru_head) lru_head->prev = page;
                lru_head = page;
                
                if (!lru_tail) lru_tail = page;
            }
            return;
        }
        page = page->next;
    }
    
    add_to_lru(virtual_addr);
}

void SwapManager::add_to_lru(u32 virtual_addr) {
    struct page_lru *page = (struct page_lru *)kmalloc(sizeof(struct page_lru));
    if (!page) return;
    
    page->virtual_addr = virtual_addr;
    page->access_time = ++access_counter;
    page->flags = 0;
    page->prev = nullptr;
    page->next = lru_head;
    
    if (lru_head) lru_head->prev = page;
    lru_head = page;
    
    if (!lru_tail) lru_tail = page;
    
    lru_count++;
}

void SwapManager::remove_from_lru(u32 virtual_addr) {
    struct page_lru *page = lru_head;
    
    while (page) {
        if (page->virtual_addr == virtual_addr) {
            if (page->prev) page->prev->next = page->next;
            if (page->next) page->next->prev = page->prev;
            
            if (page == lru_head) lru_head = page->next;
            if (page == lru_tail) lru_tail = page->prev;
            
            kfree(page);
            lru_count--;
            return;
        }
        page = page->next;
    }
}

int SwapManager::allocate_swap_entry(struct swap_device **dev_out, u32 *offset_out) {
    struct swap_device *dev = swap_devices;
    
    while (dev) {
        if (dev->inuse_pages < dev->pages) {
            for (u32 i = 0; i < dev->bitmap_size; i++) {
                if (dev->bitmap[i] != 0xFFFFFFFF) {
                    for (u32 bit = 0; bit < 32; bit++) {
                        if (!(dev->bitmap[i] & (1 << bit))) {
                            dev->bitmap[i] |= (1 << bit);
                            *dev_out = dev;
                            *offset_out = i * 32 + bit;
                            return 0;
                        }
                    }
                }
            }
        }
        dev = dev->next;
    }
    
    return -1;
}

void SwapManager::free_swap_entry(u32 entry) {
    u32 dev_id = (entry >> 24) & 0xFF;
    u32 offset = entry & 0xFFFFFF;
    
    struct swap_device *dev = swap_devices;
    for (u32 i = 0; i < dev_id && dev; i++) {
        dev = dev->next;
    }
    
    if (dev && offset < dev->pages) {
        u32 bitmap_idx = offset / 32;
        u32 bit = offset % 32;
        dev->bitmap[bitmap_idx] &= ~(1 << bit);
    }
}

struct swap_device *SwapManager::find_swap_device(const char *path) {
    struct swap_device *dev = swap_devices;
    
    while (dev) {
        if (strcmp(dev->path, path) == 0) {
            return dev;
        }
        dev = dev->next;
    }
    
    return nullptr;
}

void SwapManager::get_memory_stats(struct memory_stats *stats) {
    stats->total_pages = vmm.frame_count;
    stats->used_pages = vmm.frames_used;
    stats->free_pages = stats->total_pages - stats->used_pages;
    stats->cached_pages = 0;
    stats->swap_total = total_swap_pages;
    stats->swap_used = used_swap_pages;
    stats->swap_free = stats->swap_total - stats->swap_used;
    stats->pressure_level = check_memory_pressure();
}

void SwapManager::print_swap_stats() {
    struct memory_stats stats;
    get_memory_stats(&stats);
    
    io.print("[SWAP] Memory Statistics:\n");
    io.print("  Total pages: %d\n", stats.total_pages);
    io.print("  Used pages: %d\n", stats.used_pages);
    io.print("  Free pages: %d\n", stats.free_pages);
    io.print("  Swap total: %d pages\n", stats.swap_total);
    io.print("  Swap used: %d pages\n", stats.swap_used);
    io.print("  Swap free: %d pages\n", stats.swap_free);
    io.print("  Pressure level: %d\n", stats.pressure_level);
    io.print("  LRU pages: %d\n", lru_count);
    io.print("  Swap-ins: %d\n", swap_in_count);
    io.print("  Swap-outs: %d\n", swap_out_count);
    io.print("  Reclaim attempts: %d\n", reclaim_attempts);
}

static int swap_file_read_page(struct swap_device *dev, u32 offset, void *buffer) {
    (void)dev;
    (void)offset;
    (void)buffer;
    return -1;
}

static int swap_file_write_page(struct swap_device *dev, u32 offset, void *buffer) {
    (void)dev;
    (void)offset;
    (void)buffer;
    return -1;
}

static int swap_file_activate(struct swap_device *dev) {
    dev->pages = 65536;
    dev->bitmap_size = (dev->pages + 31) / 32;
    dev->bitmap = (u32 *)kmalloc(dev->bitmap_size * sizeof(u32));
    
    if (!dev->bitmap) {
        return -1;
    }
    
    for (u32 i = 0; i < dev->bitmap_size; i++) {
        dev->bitmap[i] = 0;
    }
    
    return 0;
}

static int swap_file_deactivate(struct swap_device *dev) {
    if (dev->bitmap) {
        kfree(dev->bitmap);
        dev->bitmap = nullptr;
    }
    
    return 0;
}

extern "C" {
void init_swap_manager() {
    swap_manager.init();
}

int swapon(const char *path, u32 flags) {
    return swap_manager.add_swap_device(path, SWAP_TYPE_FILE, flags);
}

int swapoff(const char *path) {
    return swap_manager.remove_swap_device(path);
}

u32 get_memory_pressure() {
    return swap_manager.check_memory_pressure();
}

int trigger_page_reclaim(u32 pages) {
    return swap_manager.reclaim_pages(pages);
}
}