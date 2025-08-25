#include <os.h>
#include <runtime/slab.h>

SlabAllocator slab_allocator;

static const u32 size_cache_sizes[] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
};

void SlabAllocator::init() {
    cache_chain = nullptr;
    
    for (int i = 0; i < 12; i++) {
        size_caches[i] = nullptr;
    }
    
    for (int i = 0; i < 12; i++) {
        char cache_name[32];
        u32 size = size_cache_sizes[i];
        
        for (int j = 0; j < 32; j++) {
            cache_name[j] = 0;
        }
        
        const char *base = "size-";
        int pos = 0;
        while (base[pos]) {
            cache_name[pos] = base[pos];
            pos++;
        }
        
        u32 temp = size;
        int digits = 0;
        u32 temp_copy = temp;
        while (temp_copy > 0) {
            temp_copy /= 10;
            digits++;
        }
        
        for (int d = digits - 1; d >= 0; d--) {
            cache_name[pos + d] = '0' + (temp % 10);
            temp /= 10;
        }
        
        size_caches[i] = cache_create(cache_name, size, CACHE_ALIGN, 0, nullptr, nullptr);
    }
    
    io.print("[SLAB] Initialized with %d size caches\n", 12);
}

struct slab_cache *SlabAllocator::cache_create(const char *name, u32 size, u32 align, 
                                               u32 flags, void (*ctor)(void*), void (*dtor)(void*)) {
    struct slab_cache *cache = (struct slab_cache*)buddy_alloc(sizeof(struct slab_cache));
    if (!cache) return nullptr;
    
    for (int i = 0; i < 32 && name[i]; i++) {
        cache->name[i] = name[i];
    }
    
    cache->obj_size = (size + align - 1) & ~(align - 1);
    cache->align = align;
    cache->flags = flags;
    cache->ctor = ctor;
    cache->dtor = dtor;
    
    u32 slab_size = MIN_BLOCK_SIZE;
    while (slab_size < cache->obj_size * 8 && slab_size < SLAB_MAX_SIZE) {
        slab_size *= 2;
    }
    
    cache->gfp_order = buddy_allocator.get_order(slab_size);
    cache->num_objs_per_slab = calculate_num_objs(cache->obj_size, slab_size);
    
    cache->slabs_full = nullptr;
    cache->slabs_partial = nullptr;
    cache->slabs_empty = nullptr;
    
    cache->num_slabs = 0;
    cache->num_active_objs = 0;
    cache->num_free_objs = 0;
    
    cache->next = cache_chain;
    cache_chain = cache;
    
    return cache;
}

void SlabAllocator::cache_destroy(struct slab_cache *cache) {
    if (!cache) return;
    
    while (cache->slabs_full) {
        struct slab *slab = cache->slabs_full;
        cache->slabs_full = slab->next;
        slab_destroy(cache, slab);
    }
    
    while (cache->slabs_partial) {
        struct slab *slab = cache->slabs_partial;
        cache->slabs_partial = slab->next;
        slab_destroy(cache, slab);
    }
    
    while (cache->slabs_empty) {
        struct slab *slab = cache->slabs_empty;
        cache->slabs_empty = slab->next;
        slab_destroy(cache, slab);
    }
    
    if (cache_chain == cache) {
        cache_chain = cache->next;
    } else {
        struct slab_cache *curr = cache_chain;
        while (curr && curr->next != cache) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = cache->next;
        }
    }
    
    buddy_free(cache);
}

void *SlabAllocator::cache_alloc(struct slab_cache *cache) {
    if (!cache) return nullptr;
    
    struct slab *slab = cache->slabs_partial;
    if (!slab) {
        slab = cache->slabs_empty;
        if (slab) {
            move_slab(&cache->slabs_empty, &cache->slabs_partial, slab);
        }
    }
    
    if (!slab) {
        slab = slab_create(cache);
        if (!slab) return nullptr;
        
        slab->next = cache->slabs_partial;
        if (cache->slabs_partial) {
            cache->slabs_partial->prev = slab;
        }
        cache->slabs_partial = slab;
        cache->num_slabs++;
    }
    
    void *obj = slab_alloc_obj(cache, slab);
    if (!obj) return nullptr;
    
    cache->num_active_objs++;
    cache->num_free_objs--;
    
    if (slab->free == 0) {
        move_slab(&cache->slabs_partial, &cache->slabs_full, slab);
    }
    
    if (cache->ctor) {
        cache->ctor(obj);
    }
    
    return obj;
}

void SlabAllocator::cache_free(struct slab_cache *cache, void *obj) {
    if (!cache || !obj) return;
    
    struct slab *slab = nullptr;
    struct slab *current = cache->slabs_full;
    while (current) {
        u8 *slab_start = (u8*)current->mem;
        u8 *slab_end = slab_start + (1 << current->order) * MIN_BLOCK_SIZE;
        if ((u8*)obj >= slab_start && (u8*)obj < slab_end) {
            slab = current;
            break;
        }
        current = current->next;
    }
    
    if (!slab) {
        current = cache->slabs_partial;
        while (current) {
            u8 *slab_start = (u8*)current->mem;
            u8 *slab_end = slab_start + (1 << current->order) * MIN_BLOCK_SIZE;
            if ((u8*)obj >= slab_start && (u8*)obj < slab_end) {
                slab = current;
                break;
            }
            current = current->next;
        }
    }
    
    if (!slab) return;
    
    if (cache->dtor) {
        cache->dtor(obj);
    }
    
    bool was_full = (slab->free == 0);
    slab_free_obj(cache, slab, obj);
    
    cache->num_active_objs--;
    cache->num_free_objs++;
    
    if (was_full) {
        move_slab(&cache->slabs_full, &cache->slabs_partial, slab);
    } else if (slab->free == cache->num_objs_per_slab) {
        move_slab(&cache->slabs_partial, &cache->slabs_empty, slab);
    }
}

struct slab *SlabAllocator::slab_create(struct slab_cache *cache) {
    struct slab *slab = (struct slab*)buddy_alloc(sizeof(struct slab));
    if (!slab) return nullptr;
    
    void *mem = buddy_allocator.alloc_order(cache->gfp_order);
    if (!mem) {
        buddy_free(slab);
        return nullptr;
    }
    
    slab->mem = mem;
    slab->inuse = 0;
    slab->free = cache->num_objs_per_slab;
    slab->size = cache->obj_size;
    slab->order = cache->gfp_order;
    slab->magic = SLAB_MAGIC;
    slab->next = nullptr;
    slab->prev = nullptr;
    
    u8 *obj_ptr = (u8*)mem;
    slab->freelist = nullptr;
    
    for (u32 i = 0; i < cache->num_objs_per_slab; i++) {
        struct slab_obj *obj = (struct slab_obj*)obj_ptr;
        obj->next = slab->freelist;
        obj->magic = SLAB_MAGIC;
        slab->freelist = obj;
        obj_ptr += cache->obj_size;
    }
    
    cache->num_free_objs += cache->num_objs_per_slab;
    
    return slab;
}

void SlabAllocator::slab_destroy(struct slab_cache *cache, struct slab *slab) {
    if (!slab || slab->magic != SLAB_MAGIC) return;
    
    cache->num_free_objs -= slab->free;
    buddy_allocator.free_order(slab->mem, slab->order);
    buddy_free(slab);
    cache->num_slabs--;
}

void *SlabAllocator::slab_alloc_obj(struct slab_cache *cache, struct slab *slab) {
    if (!slab->freelist) return nullptr;
    
    struct slab_obj *obj = slab->freelist;
    if (obj->magic != SLAB_MAGIC) {
        return nullptr;
    }
    
    slab->freelist = obj->next;
    slab->inuse++;
    slab->free--;
    
    return (void*)obj;
}

void SlabAllocator::slab_free_obj(struct slab_cache *cache, struct slab *slab, void *obj) {
    struct slab_obj *slab_obj = (struct slab_obj*)obj;
    slab_obj->next = slab->freelist;
    slab_obj->magic = SLAB_MAGIC;
    slab->freelist = slab_obj;
    slab->inuse--;
    slab->free++;
}

void SlabAllocator::move_slab(struct slab **from, struct slab **to, struct slab *slab) {
    if (slab->prev) {
        slab->prev->next = slab->next;
    } else {
        *from = slab->next;
    }
    
    if (slab->next) {
        slab->next->prev = slab->prev;
    }
    
    slab->next = *to;
    slab->prev = nullptr;
    if (*to) {
        (*to)->prev = slab;
    }
    *to = slab;
}

u32 SlabAllocator::calculate_num_objs(u32 obj_size, u32 slab_size) {
    return slab_size / obj_size;
}

u32 SlabAllocator::get_cache_order(u32 size) {
    return buddy_allocator.get_order(size);
}

void *SlabAllocator::kmem_cache_alloc(u32 size) {
    struct slab_cache *cache = find_size_cache(size);
    if (!cache) return nullptr;
    
    return cache_alloc(cache);
}

void SlabAllocator::kmem_cache_free(void *obj) {
    if (!obj) return;
    
    struct slab_obj *slab_obj = (struct slab_obj*)obj;
    if (slab_obj->magic != SLAB_MAGIC) {
        return;
    }
    
    for (int i = 0; i < 12; i++) {
        if (size_caches[i]) {
            cache_free(size_caches[i], obj);
            return;
        }
    }
}

struct slab_cache *SlabAllocator::find_size_cache(u32 size) {
    for (int i = 0; i < 12; i++) {
        if (size <= size_cache_sizes[i]) {
            return size_caches[i];
        }
    }
    return nullptr;
}

void SlabAllocator::cache_reap() {
    struct slab_cache *cache = cache_chain;
    while (cache) {
        while (cache->slabs_empty && cache->num_slabs > 1) {
            struct slab *slab = cache->slabs_empty;
            cache->slabs_empty = slab->next;
            if (cache->slabs_empty) {
                cache->slabs_empty->prev = nullptr;
            }
            slab_destroy(cache, slab);
        }
        cache = cache->next;
    }
}

void SlabAllocator::print_stats() {
    io.print("[SLAB] Cache Statistics:\n");
    
    struct slab_cache *cache = cache_chain;
    while (cache) {
        io.print("  %s: obj_size=%d, num_slabs=%d, active_objs=%d, free_objs=%d\n",
                 cache->name, cache->obj_size, cache->num_slabs, 
                 cache->num_active_objs, cache->num_free_objs);
        cache = cache->next;
    }
}

void init_slab_allocator() {
    slab_allocator.init();
}

extern "C" {
    void *slab_alloc(u32 size) {
        return slab_allocator.kmem_cache_alloc(size);
    }
    
    void slab_free(void *obj) {
        slab_allocator.kmem_cache_free(obj);
    }
    
    struct slab_cache *kmem_cache_create(const char *name, u32 size, u32 align, u32 flags) {
        return slab_allocator.cache_create(name, size, align, flags, nullptr, nullptr);
    }
}