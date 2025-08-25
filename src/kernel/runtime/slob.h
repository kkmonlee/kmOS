#ifndef SLOB_H
#define SLOB_H

#include <runtime/types.h>
#include <runtime/list.h>

#define SLOB_MAGIC 0x510B1234
#define SLOB_ALIGN 8
#define SLOB_BLOCK_SIZE 4096
#define SLOB_MIN_ALLOC 16
#define SLOB_MAX_ALLOC 512

struct slob_block {
    u32 size;
    u32 magic;
    struct slob_block *next;
    struct slob_block *prev;
};

struct slob_page {
    void *page_addr;
    u32 total_size;
    u32 free_size;
    u32 fragmentation;
    struct slob_block *free_list;
    struct slob_page *next;
    struct slob_page *prev;
};

class SLOBAllocator {
public:
    void init();
    void *alloc(u32 size);
    void free(void *ptr);
    void print_stats();
    void defragment();
    u32 get_efficiency();
    
private:
    struct slob_page *pages;
    u32 total_pages;
    u32 total_allocated;
    u32 total_free;
    u32 fragmentation_threshold;
    
    struct slob_page *alloc_page();
    void free_page(struct slob_page *page);
    struct slob_block *find_free_block(u32 size);
    void split_block(struct slob_block *block, u32 size);
    void merge_free_blocks(struct slob_page *page);
    void add_free_block(struct slob_page *page, struct slob_block *block);
    void remove_free_block(struct slob_page *page, struct slob_block *block);
    u32 calculate_overhead(u32 size);
    bool should_defragment();
};

extern SLOBAllocator slob_allocator;

extern "C" {
    void init_slob_allocator();
    void *slob_alloc(u32 size);
    void slob_free(void *ptr);
    void slob_stats();
}

#endif