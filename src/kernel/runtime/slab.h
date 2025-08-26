#ifndef SLAB_H
#define SLAB_H

#include <runtime/types.h>
#include <runtime/list.h>
#include <runtime/buddy.h>

#define SLAB_MAGIC 0x5AB1234
#define MAX_SLABS_PER_CACHE 256
#define CACHE_ALIGN 8
#define SLAB_MAX_SIZE 4096

struct slab_obj {
    struct slab_obj *next;
    u32 magic;
};

struct slab {
    struct slab *next;
    struct slab *prev;
    void *mem;
    u32 inuse;
    u32 free;
    struct slab_obj *freelist;
    u32 size;
    u32 order;
    u32 magic;
};

struct slab_cache {
    struct slab_cache *next;
    char name[32];
    u32 obj_size;
    u32 align;
    u32 flags;
    u32 num_objs_per_slab;
    u32 gfp_order;
    
    struct slab *slabs_full;
    struct slab *slabs_partial;
    struct slab *slabs_empty;
    
    u32 num_slabs;
    u32 num_active_objs;
    u32 num_free_objs;
    
    void (*ctor)(void *obj);
    void (*dtor)(void *obj);
};

#define SLAB_NO_REAP    0x00000001
#define SLAB_HWCACHE    0x00000002
#define SLAB_CACHE_DMA  0x00000004

class SlabAllocator {
public:
    void init();
    struct slab_cache *cache_create(const char *name, u32 size, u32 align, u32 flags,
                                   void (*ctor)(void*), void (*dtor)(void*));
    void cache_destroy(struct slab_cache *cache);
    void *cache_alloc(struct slab_cache *cache);
    void cache_free(struct slab_cache *cache, void *obj);
    void cache_reap();
    void print_stats();
    
    void *kmem_cache_alloc(u32 size);
    void kmem_cache_free(void *obj);
    
private:
    struct slab_cache *cache_chain;
    struct slab_cache *size_caches[12];
    
    struct slab *slab_create(struct slab_cache *cache);
    void slab_destroy(struct slab_cache *cache, struct slab *slab);
    void *slab_alloc_obj(struct slab_cache *cache, struct slab *slab);
    void slab_free_obj(struct slab_cache *cache, struct slab *slab, void *obj);
    void move_slab(struct slab **from, struct slab **to, struct slab *slab);
    u32 calculate_num_objs(u32 obj_size, u32 slab_size);
    u32 get_cache_order(u32 size);
    struct slab_cache *find_size_cache(u32 size);
};

extern SlabAllocator slab_allocator;

extern "C" {
    void init_slab_allocator();
    void *slab_alloc(u32 size);
    void slab_free(void *obj);
    struct slab_cache *kmem_cache_create(const char *name, u32 size, u32 align, u32 flags);
}

#endif