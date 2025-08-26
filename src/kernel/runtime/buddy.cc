#include <os.h>
#include <runtime/buddy.h>

BuddyAllocator buddy_allocator;

void BuddyAllocator::init(void *memory_start, u32 memory_size) {
    zone.memory_start = memory_start;
    zone.memory_size = memory_size;
    zone.total_blocks = memory_size / MIN_BLOCK_SIZE;
    zone.allocated_blocks = 0;
    
    for (int i = 0; i <= MAX_ORDER; i++) {
        zone.free_lists[i] = nullptr;
        zone.free_blocks[i] = 0;
    }
    
    u32 max_order = 0;
    u32 remaining_size = memory_size;
    
    while ((1U << (max_order + 1)) * MIN_BLOCK_SIZE <= remaining_size && max_order < MAX_ORDER) {
        max_order++;
    }
    
    u8 *current_addr = (u8*)memory_start;
    u32 current_size = memory_size;
    
    while (current_size >= MIN_BLOCK_SIZE) {
        u32 order = 0;
        while ((1U << (order + 1)) * MIN_BLOCK_SIZE <= current_size && order < MAX_ORDER) {
            order++;
        }
        
        struct buddy_block *block = (struct buddy_block*)current_addr;
        block->next = nullptr;
        block->prev = nullptr;
        block->order = order;
        block->magic = BUDDY_MAGIC;
        block->allocated = 0;
        
        add_to_free_list(block, order);
        
        u32 block_size = (1U << order) * MIN_BLOCK_SIZE;
        current_addr += block_size;
        current_size -= block_size;
    }
    
    io.print("[BUDDY] Initialized with %d KB available\n", memory_size / 1024);
}

u32 BuddyAllocator::get_order(u32 size) {
    u32 order = 0;
    u32 block_size = MIN_BLOCK_SIZE;
    
    while (block_size < size && order < MAX_ORDER) {
        order++;
        block_size <<= 1;
    }
    
    return order;
}

void *BuddyAllocator::alloc(u32 size) {
    if (size == 0) return nullptr;
    
    u32 order = get_order(size);
    return alloc_order(order);
}

void *BuddyAllocator::alloc_order(u32 order) {
    if (order > MAX_ORDER) return nullptr;
    
    u32 current_order = order;
    
    while (current_order <= MAX_ORDER && zone.free_lists[current_order] == nullptr) {
        current_order++;
    }
    
    if (current_order > MAX_ORDER) {
        return nullptr;
    }
    
    struct buddy_block *block = zone.free_lists[current_order];
    remove_from_free_list(block, current_order);
    
    if (current_order > order) {
        split_block(block, current_order, order);
    }
    
    block->allocated = 1;
    zone.allocated_blocks++;
    
    return (void*)((u8*)block + sizeof(struct buddy_block));
}

void BuddyAllocator::free(void *ptr) {
    if (!ptr) return;
    
    struct buddy_block *block = (struct buddy_block*)((u8*)ptr - sizeof(struct buddy_block));
    
    if (block->magic != BUDDY_MAGIC || !block->allocated) {
        io.print("[BUDDY] Error: Invalid block or double free\n");
        return;
    }
    
    free_order(ptr, block->order);
}

void BuddyAllocator::free_order(void *ptr, u32 order) {
    if (!ptr || order > MAX_ORDER) return;
    
    struct buddy_block *block = (struct buddy_block*)((u8*)ptr - sizeof(struct buddy_block));
    block->allocated = 0;
    zone.allocated_blocks--;
    
    coalesce_block(block, order);
}

void BuddyAllocator::add_to_free_list(struct buddy_block *block, u32 order) {
    block->next = zone.free_lists[order];
    block->prev = nullptr;
    
    if (zone.free_lists[order]) {
        zone.free_lists[order]->prev = block;
    }
    
    zone.free_lists[order] = block;
    zone.free_blocks[order]++;
}

void BuddyAllocator::remove_from_free_list(struct buddy_block *block, u32 order) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        zone.free_lists[order] = block->next;
    }
    
    if (block->next) {
        block->next->prev = block->prev;
    }
    
    zone.free_blocks[order]--;
}

struct buddy_block *BuddyAllocator::find_buddy(struct buddy_block *block, u32 order) {
    u32 block_size = (1U << order) * MIN_BLOCK_SIZE;
    u32 block_index = ((u8*)block - (u8*)zone.memory_start) / block_size;
    u32 buddy_index = block_index ^ 1;
    u8 *buddy_addr = (u8*)zone.memory_start + (buddy_index * block_size);
    
    if (buddy_addr >= (u8*)zone.memory_start + zone.memory_size) {
        return nullptr;
    }
    
    struct buddy_block *buddy = (struct buddy_block*)buddy_addr;
    
    if (buddy->magic == BUDDY_MAGIC && !buddy->allocated && buddy->order == order) {
        return buddy;
    }
    
    return nullptr;
}

void BuddyAllocator::split_block(struct buddy_block *block, u32 order, u32 target_order) {
    while (order > target_order) {
        order--;
        u32 half_size = (1U << order) * MIN_BLOCK_SIZE;
        
        struct buddy_block *buddy = (struct buddy_block*)((u8*)block + half_size);
        buddy->next = nullptr;
        buddy->prev = nullptr;
        buddy->order = order;
        buddy->magic = BUDDY_MAGIC;
        buddy->allocated = 0;
        
        add_to_free_list(buddy, order);
        
        block->order = order;
    }
}

void BuddyAllocator::coalesce_block(struct buddy_block *block, u32 order) {
    while (order < MAX_ORDER) {
        struct buddy_block *buddy = find_buddy(block, order);
        
        if (!buddy) {
            break;
        }
        
        remove_from_free_list(buddy, order);
        
        if (buddy < block) {
            block = buddy;
        }
        
        order++;
        block->order = order;
    }
    
    add_to_free_list(block, order);
}

void BuddyAllocator::print_stats() {
    io.print("[BUDDY] Memory Statistics:\n");
    io.print("  Total blocks: %d\n", zone.total_blocks);
    io.print("  Allocated blocks: %d\n", zone.allocated_blocks);
    
    for (u32 i = 0; i <= MAX_ORDER; i++) {
        if (zone.free_blocks[i] > 0) {
            u32 block_size = (1U << i) * MIN_BLOCK_SIZE;
            io.print("  Order %d (%d KB): %d free blocks\n", 
                     i, block_size / 1024, zone.free_blocks[i]);
        }
    }
}

void init_buddy_allocator() {
    buddy_allocator.init((void*)0x1000000, 0x1000000);
}

extern "C" {
    void *buddy_alloc(u32 size) {
        return buddy_allocator.alloc(size);
    }
    
    void buddy_free(void *ptr) {
        buddy_allocator.free(ptr);
    }
}