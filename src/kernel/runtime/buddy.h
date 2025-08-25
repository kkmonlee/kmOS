#ifndef BUDDY_H
#define BUDDY_H

#include <runtime/types.h>
#include <runtime/list.h>

#define MAX_ORDER 11
#define MIN_BLOCK_SIZE 4096
#define BUDDY_MAGIC 0xBDD1234

struct buddy_block {
    struct buddy_block *next;
    struct buddy_block *prev;
    u32 order;
    u32 magic;
    u8 allocated;
};

struct buddy_zone {
    struct buddy_block *free_lists[MAX_ORDER + 1];
    u32 free_blocks[MAX_ORDER + 1];
    void *memory_start;
    u32 memory_size;
    u32 total_blocks;
    u32 allocated_blocks;
};

class BuddyAllocator {
public:
    void init(void *memory_start, u32 memory_size);
    void *alloc(u32 size);
    void free(void *ptr);
    u32 get_order(u32 size);
    void *alloc_order(u32 order);
    void free_order(void *ptr, u32 order);
    void print_stats();
    
private:
    struct buddy_zone zone;
    
    void add_to_free_list(struct buddy_block *block, u32 order);
    void remove_from_free_list(struct buddy_block *block, u32 order);
    struct buddy_block *find_buddy(struct buddy_block *block, u32 order);
    void split_block(struct buddy_block *block, u32 order, u32 target_order);
    void coalesce_block(struct buddy_block *block, u32 order);
};

extern BuddyAllocator buddy_allocator;

extern "C" {
    void init_buddy_allocator();
    void *buddy_alloc(u32 size);
    void buddy_free(void *ptr);
}

#endif