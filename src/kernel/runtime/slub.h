#ifndef SLUB_H
#define SLUB_H

#include <runtime/types.h>
#include <runtime/list.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define SLUB_MAGIC 0x51B41234
#define SLUB_MAX_CPUS 32
#define SLUB_ALIGN 16
#define SLUB_MIN_OBJECTS 16
#define SLUB_MAX_OBJECTS 64
#define SLUB_BATCH_SIZE 16
#define SLUB_MAX_ORDER 3

struct slub_object {
    struct slub_object *next;
    u32 magic;
};

struct slub_page {
    void *page_base;
    void *freelist;
    u32 inuse;
    u32 objects;
    u32 order;
    struct slub_page *next;
    struct slub_cache *cache;
    u32 frozen;
};

struct slub_cpu_cache {
    void *freelist;
    struct slub_page *page;
    u32 batch_count;
    u32 batchlimit;
    void *batch_objects[SLUB_BATCH_SIZE];
};

struct slub_cache {
    struct slub_cache *next;
    char name[32];
    u32 size;
    u32 objsize;
    u32 align;
    u32 flags;
    u32 objects_per_page;
    u32 order;
    
    struct slub_cpu_cache cpu_caches[SLUB_MAX_CPUS];
    
    struct slub_page *partial_pages;
    struct slub_page *full_pages;
    
    u32 total_pages;
    u32 active_objects;
    u32 total_objects;
    u32 allocations;
    u32 frees;
    u32 cache_hits;
    u32 cache_misses;
    
    void (*ctor)(void *obj);
    void (*dtor)(void *obj);
};

#define SLUB_CACHE_PERCPU   0x00000001
#define SLUB_CACHE_DMA      0x00000002
#define SLUB_CACHE_ATOMIC   0x00000004
#define SLUB_CACHE_NOFREE   0x00000008

class SLUBAllocator {
public:
    void init();
    struct slub_cache *cache_create(const char *name, u32 size, u32 align, u32 flags,
                                   void (*ctor)(void*), void (*dtor)(void*));
    void cache_destroy(struct slub_cache *cache);
    void *cache_alloc(struct slub_cache *cache);
    void cache_free(struct slub_cache *cache, void *obj);
    void *alloc(u32 size);
    void free(void *obj);
    void print_stats();
    void flush_cpu_caches();
    
private:
    struct slub_cache *cache_chain;
    struct slub_cache *size_caches[16];
    u32 current_cpu;
    
    struct slub_page *alloc_slub_page(struct slub_cache *cache);
    void free_slub_page(struct slub_cache *cache, struct slub_page *page);
    void *alloc_from_page(struct slub_cache *cache, struct slub_page *page);
    void free_to_page(struct slub_cache *cache, struct slub_page *page, void *obj);
    void init_page_objects(struct slub_cache *cache, struct slub_page *page);
    void move_page_to_full(struct slub_cache *cache, struct slub_page *page);
    void move_page_to_partial(struct slub_cache *cache, struct slub_page *page);
    void remove_page_from_list(struct slub_page **list, struct slub_page *page);
    void add_page_to_list(struct slub_page **list, struct slub_page *page);
    u32 calculate_order(u32 size);
    u32 calculate_objects_per_page(u32 obj_size, u32 order);
    struct slub_cache *find_size_cache(u32 size);
    u32 get_cpu_id();
    void *cpu_cache_alloc(struct slub_cache *cache);
    void cpu_cache_free(struct slub_cache *cache, void *obj);
    void refill_cpu_cache(struct slub_cache *cache);
    void drain_cpu_cache(struct slub_cache *cache, u32 cpu);
};

extern SLUBAllocator slub_allocator;

extern "C" {
    void init_slub_allocator();
    void *slub_alloc(u32 size);
    void slub_free(void *ptr);
    struct slub_cache *slub_cache_create(const char *name, u32 size, u32 align, u32 flags);
    void slub_cache_destroy(struct slub_cache *cache);
    void *slub_cache_alloc(struct slub_cache *cache);
    void slub_cache_free(struct slub_cache *cache, void *obj);
    void slub_stats();
}

#endif