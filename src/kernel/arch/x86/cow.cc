#include <os.h>
#include <cow.h>
#include <runtime/slab.h>

COWManager cow_manager;
static struct slab_cache *vma_cache = nullptr;

class COWGuard {
public:
    COWGuard() : flags(irq_save()) {}
    ~COWGuard() { irq_restore(flags); }
private:
    u32 flags;
};

static bool valid_range(page_directory *pd, u32 start, u32 end) {
    return pd && pd != kernel_directory && !((start | end) & 4095) &&
           start >= USER_OFFSET && start <= end && end <= USER_STACK;
}

static void invalidate_mapping(page_directory *pd, u32 address) {
    if (pd == current_directory)
        asm volatile("invlpg (%0)" :: "r"(address) : "memory");
}

static bool prepare_range(page_directory *pd, u32 start, u32 end,
                          page_directory_entry *saved) {
    if (!valid_range(pd, start, end) || pd->swapped_pages) return false;
    if (start == end) return true;
    for (u32 i = VADDR_PD_OFFSET(start); i <= VADDR_PD_OFFSET(end - 1); ++i) {
        saved[i] = pd->tables[i];
        if (pd->tables[i].page_size || (pd->tables[i].present && !pd->tables[i].writable)) return false;
        page_table_entry *table = vmm.get_page_table(pd, i << 22, 0);
        if (!table) continue;
        if (table == kernel_directory->page_tables[i]) return false;
        for (u32 j = 0; j < 1024; ++j) {
            u32 address = (i << 22) | (j << 12);
            if (address >= start && address < end && (table[j].present || table[j].swapped))
                return false;
        }
    }
    return true;
}

static void rollback_range(page_directory *pd, u32 start, u32 end,
                           const page_directory_entry *saved) {
    for (u32 i = VADDR_PD_OFFSET(start); i <= VADDR_PD_OFFSET(end - 1); ++i) {
        page_table_entry *table = vmm.get_page_table(pd, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            u32 address = (i << 22) | (j << 12);
            if (address >= start && address < end) vmm.unmap_page(pd, address);
        }
        if (!saved[i].present) {
            vmm.free_frame(pd->tables[i].frame << 12);
            pd->page_tables[i] = nullptr;
        }
        pd->tables[i] = saved[i];
    }
}

void COWManager::init() {
    vma_cache = kmem_cache_create("vm_areas", sizeof(struct vm_area), 8, 0);
    if (!vma_cache) {
        io.print("[COW] Failed to create VMA cache\n");
        return;
    }
    io.print("[COW] Copy-on-Write manager initialized\n");
}

int COWManager::handle_cow_fault(page_directory *pd, u32 address, u32 error_code) {
    if ((error_code & ~4U) != 3 || address < USER_OFFSET || address >= USER_STACK) return -1;
    COWGuard guard;
    if (!pd || !pd->tables[VADDR_PD_OFFSET(address)].user ||
        !pd->tables[VADDR_PD_OFFSET(address)].writable || pd->tables[VADDR_PD_OFFSET(address)].page_size)
        return -1;
    page_table_entry *table = vmm.get_page_table(pd, address, 0);
    if (!table) return -1;
    page_table_entry *page = &table[VADDR_PT_OFFSET(address)];
    if (!page->present || !page->user || !page->cow || page->writable) return -1;
    u32 old_frame = page->frame << 12;
    u32 references = vmm.frame_references(old_frame);
    if (!references) return -1;
    if (references > 1) {
        u32 new_frame = vmm.alloc_frame();
        if (!new_frame) return -1;
        for (u32 i = 0; i < 4096; ++i) ((u8 *)new_frame)[i] = ((const u8 *)old_frame)[i];
        page->frame = new_frame >> 12;
    }
    page->cow = 0;
    page->writable = 1;
    invalidate_mapping(pd, address);
    if (references > 1) vmm.free_frame(old_frame);
    return 0;
}

int COWManager::cow_copy_page_range(page_directory *dst, page_directory *src, u32 start, u32 end) {
    if (!src || src == dst || src->swapped_pages) return -1;
    COWGuard guard;
    page_directory_entry saved[1024];
    if (!prepare_range(dst, start, end, saved)) return -1;
    if (start == end) return 0;

    for (u32 i = VADDR_PD_OFFSET(start); i <= VADDR_PD_OFFSET(end - 1); ++i) {
        if (src->tables[i].page_size) return -1;
        page_table_entry *table = vmm.get_page_table(src, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            u32 address = (i << 22) | (j << 12);
            if (address < start || address >= end) continue;
            if (table[j].swapped) return -1;
            if (!table[j].present || !table[j].user) continue;
            if (!src->tables[i].user || !src->tables[i].writable ||
                !vmm.frame_references(table[j].frame << 12) ||
                (table[j].cow && table[j].writable)) return -1;
        }
    }

    for (u32 i = VADDR_PD_OFFSET(start); i <= VADDR_PD_OFFSET(end - 1); ++i) {
        page_table_entry *table = vmm.get_page_table(src, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            u32 address = (i << 22) | (j << 12);
            if (address < start || address >= end || !table[j].present || !table[j].user) continue;
            page_table_entry *child_table = vmm.get_page_table(dst, address, 1);
            if (!child_table || !vmm.retain_frame(table[j].frame << 12)) {
                rollback_range(dst, start, end, saved);
                return -1;
            }
            child_table[j] = table[j];
            if (table[j].writable) {
                child_table[j].writable = 0;
                child_table[j].cow = 1;
            }
            dst->tables[i].user = 1;
            invalidate_mapping(dst, address);
        }
    }

    for (u32 i = VADDR_PD_OFFSET(start); i <= VADDR_PD_OFFSET(end - 1); ++i) {
        page_table_entry *table = vmm.get_page_table(src, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            u32 address = (i << 22) | (j << 12);
            if (address < start || address >= end || !table[j].present || !table[j].user) continue;
            if (table[j].writable) {
                table[j].writable = 0;
                table[j].cow = 1;
                invalidate_mapping(src, address);
            }
        }
    }
    return 0;
}

void COWManager::cow_free_page_range(page_directory *pd, u32 start, u32 end) {
    if (!valid_range(pd, start, end) || start == end) return;
    COWGuard guard;
    for (u32 i = VADDR_PD_OFFSET(start); i <= VADDR_PD_OFFSET(end - 1); ++i) {
        page_table_entry *table = vmm.get_page_table(pd, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            u32 address = (i << 22) | (j << 12);
            if (address >= start && address < end && table[j].present && table[j].user)
                vmm.unmap_page(pd, address);
        }
    }
}

struct vm_area *COWManager::create_vma(u32 start, u32 end, u32 flags) {
    struct vm_area *vma = (struct vm_area *)slab_allocator.cache_alloc(vma_cache);
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
    if (vma) slab_allocator.cache_free(vma_cache, vma);
}

int COWManager::map_pages(page_directory *pd, u32 start, u32 end, u32 flags) {
    COWGuard guard;
    page_directory_entry saved[1024];
    if (!(flags & PG_USER) || !(flags & PG_PRESENT) || !prepare_range(pd, start, end, saved)) return -1;
    for (u32 address = start; address < end; address += 4096) {
        u32 frame = vmm.alloc_frame();
        if (frame) {
            for (u32 i = 0; i < 4096; ++i) ((u8 *)frame)[i] = 0;
            if (vmm.map_page(pd, address, frame, flags) == 0) continue;
            vmm.free_frame(frame);
        }
        rollback_range(pd, start, end, saved);
        return -1;
    }
    return 0;
}

int COWManager::unmap_pages(page_directory *pd, u32 start, u32 end) {
    if (!valid_range(pd, start, end)) return -1;
    cow_free_page_range(pd, start, end);
    return 0;
}

void COWManager::cleanup_process_cow(page_directory *pd) {
    cow_free_page_range(pd, USER_OFFSET, USER_STACK);
}

void COWManager::optimize_cow_pages() {
    COWGuard guard;
    if (!current_directory) return;
    for (u32 i = VADDR_PD_OFFSET(USER_OFFSET); i < VADDR_PD_OFFSET(USER_STACK); ++i) {
        page_table_entry *table = vmm.get_page_table(current_directory, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            if (table[j].present && table[j].user && table[j].cow &&
                vmm.frame_references(table[j].frame << 12) == 1)
                handle_cow_fault(current_directory, (i << 22) | (j << 12), 3);
        }
    }
}

int COWManager::validate_cow_integrity() {
    COWGuard guard;
    if (!current_directory) return 0;
    u32 errors = 0;
    for (u32 i = VADDR_PD_OFFSET(USER_OFFSET); i < VADDR_PD_OFFSET(USER_STACK); ++i) {
        page_table_entry *table = vmm.get_page_table(current_directory, i << 22, 0);
        if (!table) continue;
        for (u32 j = 0; j < 1024; ++j) {
            if (table[j].cow && (!table[j].present || !table[j].user || table[j].writable ||
                !vmm.frame_references(table[j].frame << 12))) ++errors;
        }
    }
    return errors;
}

void COWManager::print_stats() {
    COWGuard guard;
    u32 mappings = 0, shared = 0;
    if (current_directory) {
        for (u32 i = VADDR_PD_OFFSET(USER_OFFSET); i < VADDR_PD_OFFSET(USER_STACK); ++i) {
            page_table_entry *table = vmm.get_page_table(current_directory, i << 22, 0);
            if (!table) continue;
            for (u32 j = 0; j < 1024; ++j) {
                if (!table[j].present || !table[j].user || !table[j].cow) continue;
                ++mappings;
                if (vmm.frame_references(table[j].frame << 12) > 1) ++shared;
            }
        }
    }
    io.print("[COW] Current address space: %d COW mappings, %d shared\n", mappings, shared);
}

void init_cow_manager() {
    cow_manager.init();
}

extern "C" {
    int cow_fork_mm(page_directory *child, page_directory *parent) {
        return cow_manager.cow_copy_page_range(child, parent, USER_OFFSET, USER_STACK);
    }

    int cow_handle_page_fault(u32 address, u32 error_code) {
        return cow_manager.handle_cow_fault(current_directory, address, error_code);
    }

    void cow_cleanup_process(page_directory *pd) {
        cow_manager.cleanup_process_cow(pd);
    }

    void cow_optimize() { cow_manager.optimize_cow_pages(); }
    int cow_validate() { return cow_manager.validate_cow_integrity(); }
    void cow_print_stats() { cow_manager.print_stats(); }
}
