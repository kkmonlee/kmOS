#include <os.h>
#include <cow.h>
#include <runtime/slab.h>

COWManager cow_manager;
static struct slab_cache *cow_page_cache = nullptr;
static struct slab_cache *cow_mapping_cache = nullptr;
static struct slab_cache *vma_cache = nullptr;

void COWManager::init() {
    cow_pages = nullptr;
    cow_mappings = nullptr;
    total_cow_pages = 0;
    total_cow_mappings = 0;
    
    cow_page_cache = kmem_cache_create("cow_pages", sizeof(struct cow_page), 8, 0);
    cow_mapping_cache = kmem_cache_create("cow_mappings", sizeof(struct cow_mapping), 8, 0);
    vma_cache = kmem_cache_create("vm_areas", sizeof(struct vm_area), 8, 0);
    
    if (!cow_page_cache || !cow_mapping_cache || !vma_cache) {
        io.print("[COW] Failed to create slab caches\n");
        return;
    }
    
    io.print("[COW] Copy-on-Write manager initialized\n");
}

int COWManager::handle_cow_fault(struct page_directory *pd, u32 virtual_addr, u32 error_code) {
    if (!(error_code & 0x2)) {
        return -1;
    }
    
    u32 page_addr = virtual_addr & ~0xFFF;
    u32 physical_addr = vmm.get_physical_addr(pd, page_addr);
    
    if (!physical_addr) {
        return -1;
    }
    
    struct cow_page *cow_page = find_cow_page(physical_addr);
    if (!cow_page || cow_page->ref_count <= 1) {
        struct page_table_entry *table = vmm.get_page_table(pd, page_addr, 0);
        if (table) {
            u32 page_idx = VADDR_PT_OFFSET(page_addr);
            table[page_idx].writable = 1;
            
            asm volatile("mov %0, %%cr3" :: "r"(pd->physical_address));
            return 0;
        }
        return -1;
    }
    
    return break_cow(pd, page_addr);
}

int COWManager::break_cow(struct page_directory *pd, u32 virtual_addr) {
    u32 old_physical = vmm.get_physical_addr(pd, virtual_addr);
    if (!old_physical) return -1;
    
    struct cow_page *cow_page = find_cow_page(old_physical);
    if (!cow_page) {
        struct page_table_entry *table = vmm.get_page_table(pd, virtual_addr, 0);
        if (table) {
            u32 page_idx = VADDR_PT_OFFSET(virtual_addr);
            table[page_idx].writable = 1;
            asm volatile("mov %0, %%cr3" :: "r"(pd->physical_address));
            return 0;
        }
        return -1;
    }
    
    if (cow_page->ref_count == 1) {
        struct page_table_entry *table = vmm.get_page_table(pd, virtual_addr, 0);
        if (table) {
            u32 page_idx = VADDR_PT_OFFSET(virtual_addr);
            table[page_idx].writable = 1;
            dec_cow_ref(cow_page);
            asm volatile("mov %0, %%cr3" :: "r"(pd->physical_address));
            return 0;
        }
        return -1;
    }
    
    u32 new_physical = vmm.alloc_frame();
    if (!new_physical) return -1;
    
    for (int i = 0; i < 4096; i++) {
        ((char*)new_physical)[i] = ((char*)old_physical)[i];
    }
    
    vmm.unmap_page(pd, virtual_addr);
    int result = vmm.map_page(pd, virtual_addr, new_physical, PG_PRESENT | PG_WRITE | PG_USER);
    
    if (result == 0) {
        dec_cow_ref(cow_page);
        asm volatile("mov %0, %%cr3" :: "r"(pd->physical_address));
    } else {
        vmm.free_frame(new_physical);
    }
    
    return result;
}

int COWManager::cow_copy_page_range(struct page_directory *dst_pd, struct page_directory *src_pd,
                                   u32 start_addr, u32 end_addr) {
    for (u32 addr = start_addr; addr < end_addr; addr += 4096) {
        u32 physical_addr = vmm.get_physical_addr(src_pd, addr);
        if (!physical_addr) continue;
        
        struct page_table_entry *src_table = vmm.get_page_table(src_pd, addr, 0);
        if (!src_table) continue;
        
        u32 page_idx = VADDR_PT_OFFSET(addr);
        if (!src_table[page_idx].present) continue;
        
        u32 flags = 0;
        if (src_table[page_idx].present) flags |= PG_PRESENT;
        if (src_table[page_idx].user) flags |= PG_USER;
        
        if (src_table[page_idx].writable) {
            src_table[page_idx].writable = 0;
            flags &= ~PG_WRITE;
            
            struct cow_page *cow_page = find_cow_page(physical_addr);
            if (!cow_page) {
                cow_page = alloc_cow_page(physical_addr);
                if (!cow_page) return -1;
            }
            inc_cow_ref(cow_page);
        }
        
        int result = vmm.map_page(dst_pd, addr, physical_addr, flags);
        if (result != 0) return result;
    }
    
    asm volatile("mov %0, %%cr3" :: "r"(src_pd->physical_address));
    return 0;
}

void COWManager::cow_free_page_range(struct page_directory *pd, u32 start_addr, u32 end_addr) {
    for (u32 addr = start_addr; addr < end_addr; addr += 4096) {
        u32 physical_addr = vmm.get_physical_addr(pd, addr);
        if (!physical_addr) continue;
        
        struct cow_page *cow_page = find_cow_page(physical_addr);
        if (cow_page) {
            dec_cow_ref(cow_page);
        }
        
        vmm.unmap_page(pd, addr);
    }
}

struct vm_area *COWManager::create_vma(u32 start, u32 end, u32 flags) {
    struct vm_area *vma = (struct vm_area*)slab_allocator.cache_alloc(vma_cache);
    if (!vma) return nullptr;
    
    vma->vm_start = start;
    vma->vm_end = end;
    vma->vm_flags = flags;
    vma->vm_pgoff = 0;
    vma->vm_next = nullptr;
    vma->vm_prev = nullptr;
    vma->vm_pd = nullptr;
    
    return vma;
}

void COWManager::destroy_vma(struct vm_area *vma) {
    if (!vma) return;
    slab_allocator.cache_free(vma_cache, vma);
}

struct cow_page *COWManager::alloc_cow_page(u32 physical_addr) {
    struct cow_page *page = (struct cow_page*)slab_allocator.cache_alloc(cow_page_cache);
    if (!page) return nullptr;
    
    page->physical_addr = physical_addr;
    page->ref_count = 1;
    page->magic = COW_MAGIC;
    page->next = cow_pages;
    page->prev = nullptr;
    
    if (cow_pages) {
        cow_pages->prev = page;
    }
    cow_pages = page;
    
    total_cow_pages++;
    return page;
}

void COWManager::free_cow_page(struct cow_page *page) {
    if (!page || page->magic != COW_MAGIC) return;
    
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        cow_pages = page->next;
    }
    
    if (page->next) {
        page->next->prev = page->prev;
    }
    
    slab_allocator.cache_free(cow_page_cache, page);
    total_cow_pages--;
}

struct cow_page *COWManager::find_cow_page(u32 physical_addr) {
    struct cow_page *page = cow_pages;
    while (page) {
        if (page->physical_addr == physical_addr && page->magic == COW_MAGIC) {
            return page;
        }
        page = page->next;
    }
    return nullptr;
}

void COWManager::inc_cow_ref(struct cow_page *page) {
    if (page && page->magic == COW_MAGIC && page->ref_count < MAX_COW_REFS) {
        page->ref_count++;
    }
}

void COWManager::dec_cow_ref(struct cow_page *page) {
    if (!page || page->magic != COW_MAGIC) return;
    
    if (page->ref_count > 0) {
        page->ref_count--;
    }
    
    if (page->ref_count == 0) {
        vmm.free_frame(page->physical_addr);
        free_cow_page(page);
    }
}

int COWManager::map_pages(struct page_directory *pd, u32 start_addr, u32 end_addr, u32 flags) {
    for (u32 addr = start_addr; addr < end_addr; addr += 4096) {
        u32 frame = vmm.alloc_frame();
        if (!frame) return -1;
        
        int result = vmm.map_page(pd, addr, frame, flags);
        if (result != 0) {
            vmm.free_frame(frame);
            return result;
        }
    }
    return 0;
}

int COWManager::unmap_pages(struct page_directory *pd, u32 start_addr, u32 end_addr) {
    for (u32 addr = start_addr; addr < end_addr; addr += 4096) {
        vmm.unmap_page(pd, addr);
    }
    return 0;
}

struct cow_mapping *COWManager::create_cow_mapping(u32 virtual_addr, u32 size, u32 flags,
                                                  struct page_directory *pd, struct cow_page *page) {
    struct cow_mapping *mapping = (struct cow_mapping*)slab_allocator.cache_alloc(cow_mapping_cache);
    if (!mapping) return nullptr;
    
    mapping->virtual_addr = virtual_addr;
    mapping->size = size;
    mapping->flags = flags;
    mapping->owner_pd = pd;
    mapping->cow_page = page;
    mapping->next = cow_mappings;
    
    cow_mappings = mapping;
    total_cow_mappings++;
    
    return mapping;
}

void COWManager::destroy_cow_mapping(struct cow_mapping *mapping) {
    if (!mapping) return;
    
    if (cow_mappings == mapping) {
        cow_mappings = mapping->next;
    } else {
        struct cow_mapping *curr = cow_mappings;
        while (curr && curr->next != mapping) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = mapping->next;
        }
    }
    
    slab_allocator.cache_free(cow_mapping_cache, mapping);
    total_cow_mappings--;
}

struct cow_mapping *COWManager::find_cow_mapping(struct page_directory *pd, u32 virtual_addr) {
    struct cow_mapping *mapping = cow_mappings;
    while (mapping) {
        if (mapping->owner_pd == pd && 
            virtual_addr >= mapping->virtual_addr && 
            virtual_addr < mapping->virtual_addr + mapping->size) {
            return mapping;
        }
        mapping = mapping->next;
    }
    return nullptr;
}

void COWManager::cleanup_process_cow(struct page_directory *pd) {
    struct cow_mapping *mapping = cow_mappings;
    struct cow_mapping *next;
    
    while (mapping) {
        next = mapping->next;
        if (mapping->owner_pd == pd) {
            if (mapping->cow_page) {
                dec_cow_ref(mapping->cow_page);
            }
            destroy_cow_mapping(mapping);
        }
        mapping = next;
    }
}

void COWManager::optimize_cow_pages() {
    struct cow_page *page = cow_pages;
    struct cow_page *next;
    u32 optimized = 0;
    
    while (page) {
        next = page->next;
        if (page->ref_count == 0) {
            vmm.free_frame(page->physical_addr);
            free_cow_page(page);
            optimized++;
        }
        page = next;
    }
    
    if (optimized > 0) {
        io.print("[COW] Optimized %d unused COW pages\n", optimized);
    }
}

int COWManager::validate_cow_integrity() {
    u32 errors = 0;
    struct cow_page *page = cow_pages;
    
    while (page) {
        if (page->magic != COW_MAGIC) {
            io.print("[COW] ERROR: Invalid magic in COW page %x\n", page->physical_addr);
            errors++;
        }
        if (page->ref_count > MAX_COW_REFS) {
            io.print("[COW] ERROR: Invalid ref_count %d in COW page %x\n", 
                     page->ref_count, page->physical_addr);
            errors++;
        }
        page = page->next;
    }
    
    return errors;
}

void COWManager::print_stats() {
    io.print("[COW] Statistics:\n");
    io.print("  Total COW pages: %d\n", total_cow_pages);
    io.print("  Total COW mappings: %d\n", total_cow_mappings);
    
    u32 total_refs = 0;
    u32 shared_pages = 0;
    struct cow_page *page = cow_pages;
    while (page) {
        total_refs += page->ref_count;
        if (page->ref_count > 1) {
            shared_pages++;
        }
        page = page->next;
    }
    io.print("  Total references: %d\n", total_refs);
    io.print("  Shared pages: %d\n", shared_pages);
    
    u32 memory_saved = shared_pages * 4096;
    io.print("  Memory saved: %d bytes\n", memory_saved);
}

void init_cow_manager() {
    cow_manager.init();
}

extern "C" {
    int cow_fork_mm(struct page_directory *child_pd, struct page_directory *parent_pd) {
        return cow_manager.cow_copy_page_range(child_pd, parent_pd, USER_OFFSET, USER_STACK);
    }
    
    int cow_handle_page_fault(u32 fault_addr, u32 error_code) {
        return cow_manager.handle_cow_fault(current_directory, fault_addr, error_code);
    }
    
    void cow_cleanup_process(struct page_directory *pd) {
        cow_manager.cleanup_process_cow(pd);
    }
    
    void cow_optimize() {
        cow_manager.optimize_cow_pages();
    }
    
    int cow_validate() {
        return cow_manager.validate_cow_integrity();
    }
    
    void cow_print_stats() {
        cow_manager.print_stats();
    }
}