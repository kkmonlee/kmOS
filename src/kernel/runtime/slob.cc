#include <os.h>
#include <runtime/slob.h>
#include <runtime/buddy.h>

SLOBAllocator slob_allocator;

void SLOBAllocator::init() {
    io.print("[SLOB] Initializing Simple List Of Blocks allocator\n");
    
    pages = 0;
    total_pages = 0;
    total_allocated = 0;
    total_free = 0;
    fragmentation_threshold = 75;
    
    struct slob_page *initial_page = alloc_page();
    if (!initial_page) {
        io.print("[SLOB] Failed to allocate initial page\n");
        return;
    }
    
    io.print("[SLOB] Initialized with %d bytes available\n", initial_page->total_size);
}

void *SLOBAllocator::alloc(u32 size) {
    if (size == 0 || size > SLOB_MAX_ALLOC) {
        return 0;
    }
    
    u32 aligned_size = (size + SLOB_ALIGN - 1) & ~(SLOB_ALIGN - 1);
    if (aligned_size < SLOB_MIN_ALLOC) {
        aligned_size = SLOB_MIN_ALLOC;
    }
    
    aligned_size += sizeof(struct slob_block);
    
    struct slob_block *block = find_free_block(aligned_size);
    if (!block) {
        struct slob_page *new_page = alloc_page();
        if (!new_page) {
            return 0;
        }
        block = find_free_block(aligned_size);
        if (!block) {
            return 0;
        }
    }
    
    if (block->size > aligned_size + sizeof(struct slob_block) + SLOB_MIN_ALLOC) {
        split_block(block, aligned_size);
    }
    
    struct slob_page *page = 0;
    for (struct slob_page *p = pages; p; p = p->next) {
        if ((u32)block >= (u32)p->page_addr && 
            (u32)block < (u32)p->page_addr + p->total_size) {
            page = p;
            break;
        }
    }
    
    if (page) {
        remove_free_block(page, block);
        page->free_size -= block->size;
    }
    
    block->magic = SLOB_MAGIC;
    total_allocated += block->size;
    
    return (void*)((u32)block + sizeof(struct slob_block));
}

void SLOBAllocator::free(void *ptr) {
    if (!ptr) return;
    
    struct slob_block *block = (struct slob_block*)((u32)ptr - sizeof(struct slob_block));
    
    if (block->magic != SLOB_MAGIC) {
        io.print("[SLOB] Invalid magic in block at %x\n", (u32)block);
        return;
    }
    
    struct slob_page *page = 0;
    for (struct slob_page *p = pages; p; p = p->next) {
        if ((u32)block >= (u32)p->page_addr && 
            (u32)block < (u32)p->page_addr + p->total_size) {
            page = p;
            break;
        }
    }
    
    if (!page) {
        io.print("[SLOB] Block not found in any page\n");
        return;
    }
    
    block->magic = 0;
    total_allocated -= block->size;
    page->free_size += block->size;
    
    add_free_block(page, block);
    merge_free_blocks(page);
    
    if (page->free_size == page->total_size - sizeof(struct slob_page)) {
        if (total_pages > 1) {
            free_page(page);
        }
    }
    
    if (should_defragment()) {
        defragment();
    }
}

struct slob_page *SLOBAllocator::alloc_page() {
    void *page_mem = buddy_allocator.alloc(SLOB_BLOCK_SIZE);
    if (!page_mem) {
        return 0;
    }
    
    struct slob_page *page = (struct slob_page*)page_mem;
    page->page_addr = page_mem;
    page->total_size = SLOB_BLOCK_SIZE;
    page->free_size = SLOB_BLOCK_SIZE - sizeof(struct slob_page);
    page->fragmentation = 0;
    page->next = pages;
    page->prev = 0;
    
    if (pages) {
        pages->prev = page;
    }
    pages = page;
    
    struct slob_block *initial_block = (struct slob_block*)((u32)page_mem + sizeof(struct slob_page));
    initial_block->size = page->free_size;
    initial_block->magic = 0;
    initial_block->next = 0;
    initial_block->prev = 0;
    
    page->free_list = initial_block;
    total_pages++;
    total_free += page->free_size;
    
    return page;
}

void SLOBAllocator::free_page(struct slob_page *page) {
    if (!page) return;
    
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        pages = page->next;
    }
    
    if (page->next) {
        page->next->prev = page->prev;
    }
    
    total_pages--;
    total_free -= page->free_size;
    
    buddy_allocator.free(page->page_addr);
}

struct slob_block *SLOBAllocator::find_free_block(u32 size) {
    struct slob_block *best_fit = 0;
    u32 best_size = 0xFFFFFFFF;
    
    for (struct slob_page *page = pages; page; page = page->next) {
        for (struct slob_block *block = page->free_list; block; block = block->next) {
            if (block->size >= size && block->size < best_size) {
                best_fit = block;
                best_size = block->size;
                
                if (best_size == size) {
                    return best_fit;
                }
            }
        }
    }
    
    return best_fit;
}

void SLOBAllocator::split_block(struct slob_block *block, u32 size) {
    if (block->size <= size + sizeof(struct slob_block)) {
        return;
    }
    
    struct slob_block *new_block = (struct slob_block*)((u32)block + size);
    new_block->size = block->size - size;
    new_block->magic = 0;
    new_block->next = block->next;
    new_block->prev = block;
    
    if (block->next) {
        block->next->prev = new_block;
    }
    block->next = new_block;
    block->size = size;
}

void SLOBAllocator::merge_free_blocks(struct slob_page *page) {
    if (!page || !page->free_list) return;
    
    struct slob_block *current = page->free_list;
    while (current && current->next) {
        struct slob_block *next = current->next;
        
        if ((u32)current + current->size == (u32)next) {
            current->size += next->size;
            current->next = next->next;
            if (next->next) {
                next->next->prev = current;
            }
            page->fragmentation--;
        } else {
            current = current->next;
        }
    }
}

void SLOBAllocator::add_free_block(struct slob_page *page, struct slob_block *block) {
    if (!page || !block) return;
    
    if (!page->free_list) {
        page->free_list = block;
        block->next = 0;
        block->prev = 0;
        return;
    }
    
    if ((u32)block < (u32)page->free_list) {
        block->next = page->free_list;
        block->prev = 0;
        page->free_list->prev = block;
        page->free_list = block;
        return;
    }
    
    struct slob_block *current = page->free_list;
    while (current->next && (u32)current->next < (u32)block) {
        current = current->next;
    }
    
    block->next = current->next;
    block->prev = current;
    if (current->next) {
        current->next->prev = block;
    }
    current->next = block;
    
    page->fragmentation++;
}

void SLOBAllocator::remove_free_block(struct slob_page *page, struct slob_block *block) {
    if (!page || !block) return;
    
    if (page->free_list == block) {
        page->free_list = block->next;
    }
    
    if (block->prev) {
        block->prev->next = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    
    block->next = 0;
    block->prev = 0;
}

void SLOBAllocator::defragment() {
    for (struct slob_page *page = pages; page; page = page->next) {
        merge_free_blocks(page);
        page->fragmentation = 0;
        
        for (struct slob_block *block = page->free_list; block; block = block->next) {
            if (block->next) {
                page->fragmentation++;
            }
        }
    }
}

bool SLOBAllocator::should_defragment() {
    u32 total_fragmentation = 0;
    for (struct slob_page *page = pages; page; page = page->next) {
        total_fragmentation += page->fragmentation;
    }
    
    if (total_pages == 0) return false;
    
    u32 avg_fragmentation = total_fragmentation / total_pages;
    return avg_fragmentation > fragmentation_threshold;
}

u32 SLOBAllocator::get_efficiency() {
    if (total_allocated + total_free == 0) return 0;
    
    return (total_allocated * 100) / (total_allocated + total_free);
}

void SLOBAllocator::print_stats() {
    io.print("[SLOB] Statistics:\n");
    io.print("  Pages: %d\n", total_pages);
    io.print("  Total allocated: %d bytes\n", total_allocated);
    io.print("  Total free: %d bytes\n", total_free);
    io.print("  Efficiency: %d%%\n", get_efficiency());
    
    u32 total_fragmentation = 0;
    for (struct slob_page *page = pages; page; page = page->next) {
        total_fragmentation += page->fragmentation;
    }
    
    if (total_pages > 0) {
        io.print("  Average fragmentation: %d blocks/page\n", total_fragmentation / total_pages);
    }
}

extern "C" {
void init_slob_allocator() {
    slob_allocator.init();
}

void *slob_alloc(u32 size) {
    return slob_allocator.alloc(size);
}

void slob_free(void *ptr) {
    slob_allocator.free(ptr);
}

void slob_stats() {
    slob_allocator.print_stats();
}
}