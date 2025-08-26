#include <os.h>
#include <runtime/slub.h>
#include <runtime/buddy.h>

extern "C" {
    char *itoa(int value, char *str, int base);
    int strcpy(char *dst, const char *src);
}

SLUBAllocator slub_allocator;

void SLUBAllocator::init() {
    io.print("[SLUB] Initializing SLaB Unqueued allocator\n");
    
    cache_chain = 0;
    current_cpu = 0;
    
    for (int i = 0; i < 16; i++) {
        size_caches[i] = 0;
    }
    
    static const u32 cache_sizes[] = {
        16, 32, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 8192
    };
    
    for (int i = 0; i < 16; i++) {
        char cache_name[32];
        itoa(cache_sizes[i], cache_name + 5, 10);
        cache_name[0] = 's'; cache_name[1] = 'l'; cache_name[2] = 'u'; 
        cache_name[3] = 'b'; cache_name[4] = '-';
        size_caches[i] = cache_create(cache_name, cache_sizes[i], SLUB_ALIGN, 
                                    SLUB_CACHE_PERCPU, 0, 0);
    }
    
    io.print("[SLUB] Initialized with %d size caches\n", 16);
}

struct slub_cache *SLUBAllocator::cache_create(const char *name, u32 size, u32 align, 
                                              u32 flags, void (*ctor)(void*), void (*dtor)(void*)) {
    struct slub_cache *cache = (struct slub_cache*)buddy_allocator.alloc(sizeof(struct slub_cache));
    if (!cache) {
        return 0;
    }
    
    strcpy(cache->name, name);
    cache->objsize = size;
    cache->size = (size + align - 1) & ~(align - 1);
    if (cache->size < sizeof(struct slub_object)) {
        cache->size = sizeof(struct slub_object);
    }
    
    cache->align = align;
    cache->flags = flags;
    cache->order = calculate_order(cache->size);
    cache->objects_per_page = calculate_objects_per_page(cache->size, cache->order);
    
    cache->partial_pages = 0;
    cache->full_pages = 0;
    cache->total_pages = 0;
    cache->active_objects = 0;
    cache->total_objects = 0;
    cache->allocations = 0;
    cache->frees = 0;
    cache->cache_hits = 0;
    cache->cache_misses = 0;
    
    cache->ctor = ctor;
    cache->dtor = dtor;
    
    for (int i = 0; i < SLUB_MAX_CPUS; i++) {
        cache->cpu_caches[i].freelist = 0;
        cache->cpu_caches[i].page = 0;
        cache->cpu_caches[i].batch_count = 0;
        cache->cpu_caches[i].batchlimit = SLUB_BATCH_SIZE / 2;
        
        for (int j = 0; j < SLUB_BATCH_SIZE; j++) {
            cache->cpu_caches[i].batch_objects[j] = 0;
        }
    }
    
    cache->next = cache_chain;
    cache_chain = cache;
    
    return cache;
}

void SLUBAllocator::cache_destroy(struct slub_cache *cache) {
    if (!cache) return;
    
    flush_cpu_caches();
    
    struct slub_page *page = cache->partial_pages;
    while (page) {
        struct slub_page *next = page->next;
        free_slub_page(cache, page);
        page = next;
    }
    
    page = cache->full_pages;
    while (page) {
        struct slub_page *next = page->next;
        free_slub_page(cache, page);
        page = next;
    }
    
    if (cache_chain == cache) {
        cache_chain = cache->next;
    } else {
        for (struct slub_cache *c = cache_chain; c; c = c->next) {
            if (c->next == cache) {
                c->next = cache->next;
                break;
            }
        }
    }
    
    buddy_allocator.free(cache);
}

void *SLUBAllocator::cache_alloc(struct slub_cache *cache) {
    if (!cache) return 0;
    
    void *obj = 0;
    
    if (cache->flags & SLUB_CACHE_PERCPU) {
        obj = cpu_cache_alloc(cache);
        if (obj) {
            cache->cache_hits++;
            cache->allocations++;
            if (cache->ctor) {
                cache->ctor(obj);
            }
            return obj;
        }
        cache->cache_misses++;
    }
    
    struct slub_page *page = cache->partial_pages;
    if (!page) {
        page = alloc_slub_page(cache);
        if (!page) {
            return 0;
        }
    }
    
    obj = alloc_from_page(cache, page);
    if (obj) {
        cache->allocations++;
        cache->active_objects++;
        
        if (page->inuse == page->objects) {
            remove_page_from_list(&cache->partial_pages, page);
            add_page_to_list(&cache->full_pages, page);
        }
        
        if (cache->ctor) {
            cache->ctor(obj);
        }
    }
    
    return obj;
}

void SLUBAllocator::cache_free(struct slub_cache *cache, void *obj) {
    if (!cache || !obj) return;
    
    struct slub_object *slub_obj = (struct slub_object*)obj;
    if (slub_obj->magic != SLUB_MAGIC) {
        io.print("[SLUB] Invalid magic in object at %x\n", (u32)obj);
        return;
    }
    
    if (cache->dtor) {
        cache->dtor(obj);
    }
    
    if (cache->flags & SLUB_CACHE_PERCPU) {
        cpu_cache_free(cache, obj);
        cache->frees++;
        return;
    }
    
    struct slub_page *page = 0;
    for (struct slub_page *p = cache->full_pages; p; p = p->next) {
        if ((u32)obj >= (u32)p->page_base && 
            (u32)obj < (u32)p->page_base + (PAGE_SIZE << p->order)) {
            page = p;
            break;
        }
    }
    
    if (!page) {
        for (struct slub_page *p = cache->partial_pages; p; p = p->next) {
            if ((u32)obj >= (u32)p->page_base && 
                (u32)obj < (u32)p->page_base + (PAGE_SIZE << p->order)) {
                page = p;
                break;
            }
        }
    }
    
    if (page) {
        bool was_full = (page->inuse == page->objects);
        
        free_to_page(cache, page, obj);
        cache->frees++;
        cache->active_objects--;
        
        if (was_full && page->inuse < page->objects) {
            remove_page_from_list(&cache->full_pages, page);
            add_page_to_list(&cache->partial_pages, page);
        }
        
        if (page->inuse == 0 && cache->total_pages > 1) {
            remove_page_from_list(&cache->partial_pages, page);
            free_slub_page(cache, page);
        }
    }
}

void *SLUBAllocator::alloc(u32 size) {
    if (size == 0) return 0;
    
    struct slub_cache *cache = find_size_cache(size);
    if (!cache) {
        return buddy_allocator.alloc(size);
    }
    
    return cache_alloc(cache);
}

void SLUBAllocator::free(void *obj) {
    if (!obj) return;
    
    for (struct slub_cache *cache = cache_chain; cache; cache = cache->next) {
        for (struct slub_page *page = cache->partial_pages; page; page = page->next) {
            if ((u32)obj >= (u32)page->page_base && 
                (u32)obj < (u32)page->page_base + (PAGE_SIZE << page->order)) {
                cache_free(cache, obj);
                return;
            }
        }
        
        for (struct slub_page *page = cache->full_pages; page; page = page->next) {
            if ((u32)obj >= (u32)page->page_base && 
                (u32)obj < (u32)page->page_base + (PAGE_SIZE << page->order)) {
                cache_free(cache, obj);
                return;
            }
        }
    }
    
    buddy_allocator.free(obj);
}

struct slub_page *SLUBAllocator::alloc_slub_page(struct slub_cache *cache) {
    void *page_mem = buddy_allocator.alloc(PAGE_SIZE << cache->order);
    if (!page_mem) {
        return 0;
    }
    
    struct slub_page *page = (struct slub_page*)buddy_allocator.alloc(sizeof(struct slub_page));
    if (!page) {
        buddy_allocator.free(page_mem);
        return 0;
    }
    
    page->page_base = page_mem;
    page->objects = cache->objects_per_page;
    page->inuse = 0;
    page->order = cache->order;
    page->cache = cache;
    page->frozen = 0;
    page->next = 0;
    
    init_page_objects(cache, page);
    add_page_to_list(&cache->partial_pages, page);
    
    cache->total_pages++;
    cache->total_objects += page->objects;
    
    return page;
}

void SLUBAllocator::init_page_objects(struct slub_cache *cache, struct slub_page *page) {
    void *obj_ptr = page->page_base;
    struct slub_object *prev = 0;
    
    for (u32 i = 0; i < page->objects; i++) {
        struct slub_object *obj = (struct slub_object*)obj_ptr;
        obj->magic = 0;
        obj->next = prev;
        prev = obj;
        obj_ptr = (void*)((u32)obj_ptr + cache->size);
    }
    
    page->freelist = prev;
}

void *SLUBAllocator::alloc_from_page(struct slub_cache * /*cache*/, struct slub_page *page) {
    if (!page->freelist) {
        return 0;
    }
    
    struct slub_object *obj = (struct slub_object*)page->freelist;
    page->freelist = obj->next;
    page->inuse++;
    
    obj->magic = SLUB_MAGIC;
    obj->next = 0;
    
    return obj;
}

void SLUBAllocator::free_to_page(struct slub_cache * /*cache*/, struct slub_page *page, void *obj) {
    struct slub_object *slub_obj = (struct slub_object*)obj;
    slub_obj->magic = 0;
    slub_obj->next = (struct slub_object*)page->freelist;
    page->freelist = obj;
    page->inuse--;
}

void *SLUBAllocator::cpu_cache_alloc(struct slub_cache *cache) {
    u32 cpu = get_cpu_id();
    if (cpu >= SLUB_MAX_CPUS) cpu = 0;
    
    struct slub_cpu_cache *cpu_cache = &cache->cpu_caches[cpu];
    
    if (cpu_cache->batch_count > 0) {
        void *obj = cpu_cache->batch_objects[--cpu_cache->batch_count];
        return obj;
    }
    
    if (cpu_cache->freelist) {
        struct slub_object *obj = (struct slub_object*)cpu_cache->freelist;
        cpu_cache->freelist = obj->next;
        return obj;
    }
    
    refill_cpu_cache(cache);
    
    if (cpu_cache->batch_count > 0) {
        void *obj = cpu_cache->batch_objects[--cpu_cache->batch_count];
        return obj;
    }
    
    return 0;
}

void SLUBAllocator::cpu_cache_free(struct slub_cache *cache, void *obj) {
    u32 cpu = get_cpu_id();
    if (cpu >= SLUB_MAX_CPUS) cpu = 0;
    
    struct slub_cpu_cache *cpu_cache = &cache->cpu_caches[cpu];
    
    if (cpu_cache->batch_count < SLUB_BATCH_SIZE) {
        cpu_cache->batch_objects[cpu_cache->batch_count++] = obj;
        return;
    }
    
    struct slub_object *slub_obj = (struct slub_object*)obj;
    slub_obj->next = (struct slub_object*)cpu_cache->freelist;
    cpu_cache->freelist = obj;
}

void SLUBAllocator::refill_cpu_cache(struct slub_cache *cache) {
    u32 cpu = get_cpu_id();
    if (cpu >= SLUB_MAX_CPUS) cpu = 0;
    
    struct slub_cpu_cache *cpu_cache = &cache->cpu_caches[cpu];
    
    for (u32 i = 0; i < cpu_cache->batchlimit; i++) {
        struct slub_page *page = cache->partial_pages;
        if (!page) {
            page = alloc_slub_page(cache);
            if (!page) break;
        }
        
        void *obj = alloc_from_page(cache, page);
        if (obj) {
            cpu_cache->batch_objects[cpu_cache->batch_count++] = obj;
            cache->active_objects++;
            
            if (page->inuse == page->objects) {
                remove_page_from_list(&cache->partial_pages, page);
                add_page_to_list(&cache->full_pages, page);
            }
        }
    }
}

u32 SLUBAllocator::calculate_order(u32 size) {
    u32 objects_per_page = PAGE_SIZE / size;
    if (objects_per_page >= SLUB_MIN_OBJECTS) {
        return 0;
    }
    
    for (u32 order = 1; order <= SLUB_MAX_ORDER; order++) {
        u32 page_size = PAGE_SIZE << order;
        if ((page_size / size) >= SLUB_MIN_OBJECTS) {
            return order;
        }
    }
    
    return SLUB_MAX_ORDER;
}

u32 SLUBAllocator::calculate_objects_per_page(u32 obj_size, u32 order) {
    u32 page_size = PAGE_SIZE << order;
    return page_size / obj_size;
}

struct slub_cache *SLUBAllocator::find_size_cache(u32 size) {
    static const u32 cache_sizes[] = {
        16, 32, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 8192
    };
    
    for (int i = 0; i < 16; i++) {
        if (size <= cache_sizes[i]) {
            return size_caches[i];
        }
    }
    
    return 0;
}

u32 SLUBAllocator::get_cpu_id() {
    return current_cpu;
}

void SLUBAllocator::flush_cpu_caches() {
    for (struct slub_cache *cache = cache_chain; cache; cache = cache->next) {
        for (u32 cpu = 0; cpu < SLUB_MAX_CPUS; cpu++) {
            drain_cpu_cache(cache, cpu);
        }
    }
}

void SLUBAllocator::drain_cpu_cache(struct slub_cache *cache, u32 cpu) {
    if (cpu >= SLUB_MAX_CPUS) return;
    
    struct slub_cpu_cache *cpu_cache = &cache->cpu_caches[cpu];
    
    while (cpu_cache->batch_count > 0) {
        void *obj = cpu_cache->batch_objects[--cpu_cache->batch_count];
        cache_free(cache, obj);
    }
    
    while (cpu_cache->freelist) {
        struct slub_object *obj = (struct slub_object*)cpu_cache->freelist;
        cpu_cache->freelist = obj->next;
        cache_free(cache, obj);
    }
}

void SLUBAllocator::add_page_to_list(struct slub_page **list, struct slub_page *page) {
    page->next = *list;
    *list = page;
}

void SLUBAllocator::remove_page_from_list(struct slub_page **list, struct slub_page *page) {
    if (*list == page) {
        *list = page->next;
        return;
    }
    
    struct slub_page *current = *list;
    while (current && current->next != page) {
        current = current->next;
    }
    
    if (current) {
        current->next = page->next;
    }
}

void SLUBAllocator::free_slub_page(struct slub_cache *cache, struct slub_page *page) {
    cache->total_pages--;
    cache->total_objects -= page->objects;
    
    buddy_allocator.free(page->page_base);
    buddy_allocator.free(page);
}

void SLUBAllocator::print_stats() {
    io.print("[SLUB] Statistics:\n");
    
    u32 total_caches = 0;
    u32 total_pages = 0;
    u32 total_objects = 0;
    u32 total_allocated = 0;
    u32 total_allocs = 0;
    u32 total_frees = 0;
    u32 total_hits = 0;
    u32 total_misses = 0;
    
    for (struct slub_cache *cache = cache_chain; cache; cache = cache->next) {
        total_caches++;
        total_pages += cache->total_pages;
        total_objects += cache->total_objects;
        total_allocated += cache->active_objects;
        total_allocs += cache->allocations;
        total_frees += cache->frees;
        total_hits += cache->cache_hits;
        total_misses += cache->cache_misses;
    }
    
    io.print("  Total caches: %d\n", total_caches);
    io.print("  Total pages: %d\n", total_pages);
    io.print("  Total objects: %d\n", total_objects);
    io.print("  Active objects: %d\n", total_allocated);
    io.print("  Allocations: %d\n", total_allocs);
    io.print("  Frees: %d\n", total_frees);
    
    if (total_hits + total_misses > 0) {
        u32 hit_rate = (total_hits * 100) / (total_hits + total_misses);
        io.print("  Cache hit rate: %d%%\n", hit_rate);
    }
}

extern "C" {
void init_slub_allocator() {
    slub_allocator.init();
}

void *slub_alloc(u32 size) {
    return slub_allocator.alloc(size);
}

void slub_free(void *ptr) {
    slub_allocator.free(ptr);
}

struct slub_cache *slub_cache_create(const char *name, u32 size, u32 align, u32 flags) {
    return slub_allocator.cache_create(name, size, align, flags, 0, 0);
}

void slub_cache_destroy(struct slub_cache *cache) {
    slub_allocator.cache_destroy(cache);
}

void *slub_cache_alloc(struct slub_cache *cache) {
    return slub_allocator.cache_alloc(cache);
}

void slub_cache_free(struct slub_cache *cache, void *obj) {
    slub_allocator.cache_free(cache, obj);
}

void slub_stats() {
    slub_allocator.print_stats();
}
}