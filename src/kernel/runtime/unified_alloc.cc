#include <os.h>
#include <runtime/unified_alloc.h>
#include <arch/x86/x86.h>

extern "C" {
    void *memset(void *s, int c, int n);
    void *memcpy(void *dest, const void *src, int n);
}

UnifiedAllocator unified_allocator;

static const u32 ALLOCATION_MAGIC = 0xa110ca7e;
static const u32 ALLOCATION_ALIGNMENT = 16;

struct allocation_header {
    u32 magic;
    u32 size;
    u32 flags;
    u32 allocator;
    void *base;
};

class AllocationGuard {
public:
    AllocationGuard() : flags(irq_save()) {}
    ~AllocationGuard() { irq_restore(flags); }
private:
    u32 flags;
};

static u32 mode_policy(enum system_mode mode) {
    switch (mode) {
        case SYS_MODE_EMBEDDED:
            return ALLOC_POLICY_SLOB | ALLOC_POLICY_BUDDY | ALLOC_POLICY_STACK;
        case SYS_MODE_SERVER:
            return ALLOC_POLICY_SLUB | ALLOC_POLICY_BUDDY | ALLOC_POLICY_STACK | ALLOC_POLICY_AUTO;
        case SYS_MODE_REALTIME:
            return ALLOC_POLICY_SLAB | ALLOC_POLICY_BUDDY | ALLOC_POLICY_STACK;
        default:
            return ALLOC_POLICY_SLAB | ALLOC_POLICY_BUDDY | ALLOC_POLICY_STACK | ALLOC_POLICY_AUTO;
    }
}

void UnifiedAllocator::init(enum system_mode mode) {
    io.print("[UNIFIED] Initializing unified allocator in %s mode\n", 
             mode == SYS_MODE_EMBEDDED ? "embedded" :
             mode == SYS_MODE_DESKTOP ? "desktop" :
             mode == SYS_MODE_SERVER ? "server" : "realtime");
    
    current_mode = mode;
    debug_tracking = false;
    debug_table = 0;
    debug_table_size = 0;
    debug_entries = 0;
    
    memset(&stats, 0, sizeof(stats));
    
    policy_mask = mode_policy(mode);
    
    io.print("[UNIFIED] Policy mask: 0x%x\n", policy_mask);
}

void *UnifiedAllocator::alloc(u32 size, u32 flags) {
    const u32 overhead = sizeof(allocation_header) + ALLOCATION_ALIGNMENT - 1;
    const u32 maximum = (MIN_BLOCK_SIZE << MAX_ORDER) - sizeof(buddy_block);
    if (!size || size > maximum - overhead) return nullptr;

    AllocationGuard guard;
    u32 allocator = select_allocator(size, flags);
    void *base = internal_alloc(size + overhead, allocator);
    if (!base) return nullptr;

    u32 address = ((u32)base + overhead) & ~(ALLOCATION_ALIGNMENT - 1);
    void *ptr = (void *)address;
    allocation_header *header = (allocation_header *)ptr - 1;
    header->magic = ALLOCATION_MAGIC;
    header->size = size;
    header->flags = flags;
    header->allocator = allocator;
    header->base = base;

    update_stats(size, allocator, true);
    if (debug_tracking) {
        track_allocation(ptr, size, flags, allocator);
    }
    if (flags & ALLOC_FLAG_ZERO) {
        memset(ptr, 0, size);
    }
    
    return ptr;
}

void UnifiedAllocator::free(void *ptr) {
    if (!ptr) return;
    
    AllocationGuard guard;
    allocation_header *header = (allocation_header *)ptr - 1;
    if (header->magic != ALLOCATION_MAGIC) return;

    u32 allocator = header->allocator;
    void *base = header->base;
    update_stats(header->size, allocator, false);
    untrack_allocation(ptr);
    header->magic = 0;
    internal_free(base, allocator);
}

void *UnifiedAllocator::realloc(void *ptr, u32 new_size) {
    if (!ptr) {
        return alloc(new_size);
    }
    
    if (new_size == 0) {
        free(ptr);
        return 0;
    }
    
    AllocationGuard guard;
    allocation_header *header = (allocation_header *)ptr - 1;
    if (header->magic != ALLOCATION_MAGIC) return nullptr;
    u32 old_size = header->size;
    
    void *new_ptr = alloc(new_size, header->flags);
    if (!new_ptr) {
        return 0;
    }
    
    if (old_size > 0) {
        u32 copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
    }
    
    free(ptr);
    return new_ptr;
}

void *UnifiedAllocator::calloc(u32 count, u32 size) {
    if (!count || !size || size > ~0U / count) return nullptr;
    return alloc(count * size, ALLOC_FLAG_ZERO);
}

u32 UnifiedAllocator::select_allocator(u32 size, u32 flags) {
    enum alloc_type type = classify_allocation(size);
    
    if (flags & (ALLOC_FLAG_TEMP | ALLOC_FLAG_SCOPED) && (policy_mask & ALLOC_POLICY_STACK)) {
        return ALLOC_POLICY_STACK;
    }
    
    if (flags & ALLOC_FLAG_DMA) {
        return ALLOC_POLICY_BUDDY;
    }
    
    switch (current_mode) {
        case SYS_MODE_EMBEDDED:
            if (type <= ALLOC_SMALL && (policy_mask & ALLOC_POLICY_SLOB)) {
                return ALLOC_POLICY_SLOB;
            }
            return ALLOC_POLICY_BUDDY;
            
        case SYS_MODE_DESKTOP:
            if (type <= ALLOC_MEDIUM && (policy_mask & ALLOC_POLICY_SLAB)) {
                return ALLOC_POLICY_SLAB;
            }
            return ALLOC_POLICY_BUDDY;
            
        case SYS_MODE_SERVER:
            if (type <= ALLOC_MEDIUM && (policy_mask & ALLOC_POLICY_SLUB)) {
                return ALLOC_POLICY_SLUB;
            }
            return ALLOC_POLICY_BUDDY;
            
        case SYS_MODE_REALTIME:
            if (type <= ALLOC_SMALL && (policy_mask & ALLOC_POLICY_SLAB)) {
                return ALLOC_POLICY_SLAB;
            }
            return ALLOC_POLICY_BUDDY;
    }
    
    return ALLOC_POLICY_BUDDY;
}

void *UnifiedAllocator::internal_alloc(u32 size, u32 &allocator) {
    void *ptr = 0;
    
    switch (allocator) {
        case ALLOC_POLICY_SLOB:
            ptr = slob_allocator.alloc(size);
            break;
            
        case ALLOC_POLICY_SLAB:
            ptr = slab_allocator.kmem_cache_alloc(size);
            break;
            
        case ALLOC_POLICY_SLUB:
            ptr = slub_allocator.alloc(size);
            break;
            
        case ALLOC_POLICY_STACK:
            ptr = global_stack_allocator.alloc(size);
            break;
            
        case ALLOC_POLICY_BUDDY:
        default:
            ptr = buddy_allocator.alloc(size);
            break;
    }
    
    if (!ptr && allocator != ALLOC_POLICY_BUDDY) {
        ptr = buddy_allocator.alloc(size);
        if (ptr) {
            allocator = ALLOC_POLICY_BUDDY;
        }
    }
    
    return ptr;
}

void UnifiedAllocator::internal_free(void *ptr, u32 allocator_hint) {
    if (allocator_hint == ALLOC_POLICY_SLOB) {
        slob_allocator.free(ptr);
    } else if (allocator_hint == ALLOC_POLICY_SLAB) {
        slab_allocator.kmem_cache_free(ptr);
    } else if (allocator_hint == ALLOC_POLICY_SLUB) {
        slub_allocator.free(ptr);
    } else if (allocator_hint == ALLOC_POLICY_BUDDY) {
        buddy_allocator.free(ptr);
    }
}

enum alloc_type UnifiedAllocator::classify_allocation(u32 size) {
    return get_alloc_type(size);
}

void UnifiedAllocator::update_stats(u32 size, u32 allocator, bool is_alloc) {
    if (is_alloc) {
        stats.total_allocations++;
        stats.active_allocations++;
        stats.bytes_allocated += size;
        
        switch (allocator) {
            case ALLOC_POLICY_BUDDY: stats.buddy_allocs++; break;
            case ALLOC_POLICY_SLAB: stats.slab_allocs++; break;
            case ALLOC_POLICY_SLOB: stats.slob_allocs++; break;
            case ALLOC_POLICY_SLUB: stats.slub_allocs++; break;
            case ALLOC_POLICY_STACK: stats.stack_allocs++; break;
        }
    } else {
        stats.total_frees++;
        stats.active_allocations--;
        stats.bytes_freed += size;
    }
}

void UnifiedAllocator::track_allocation(void *ptr, u32 size, u32 flags, u32 allocator) {
    if (!debug_table || debug_entries >= debug_table_size) {
        return;
    }
    
    struct allocation_metadata *meta = &debug_table[debug_entries++];
    meta->size = size;
    meta->flags = flags;
    meta->allocator_used = allocator;
    meta->timestamp = 0;
    meta->original_ptr = ptr;
}

void UnifiedAllocator::untrack_allocation(void *ptr) {
    for (u32 i = 0; i < debug_entries; i++) {
        if (debug_table[i].original_ptr == ptr) {
            debug_table[i] = debug_table[--debug_entries];
            return;
        }
    }
}

struct allocation_metadata *UnifiedAllocator::find_allocation(void *ptr) {
    for (u32 i = 0; i < debug_entries; i++) {
        if (debug_table[i].original_ptr == ptr) {
            return &debug_table[i];
        }
    }
    return 0;
}

void UnifiedAllocator::enable_debug_tracking() {
    AllocationGuard guard;
    if (!debug_table) {
        debug_table_size = 1024;
        debug_table = (struct allocation_metadata*)buddy_allocator.alloc(
            debug_table_size * sizeof(struct allocation_metadata));
    }
    debug_tracking = debug_table != nullptr;
    io.print("[UNIFIED] Debug tracking enabled\n");
}

void UnifiedAllocator::disable_debug_tracking() {
    AllocationGuard guard;
    debug_tracking = false;
    io.print("[UNIFIED] Debug tracking disabled\n");
}

void *UnifiedAllocator::alloc_pages(u32 order, u32 flags) {
    if (order >= MAX_ORDER) return nullptr;
    AllocationGuard guard;
    void *ptr = buddy_allocator.alloc(PAGE_SIZE << order);
    if (ptr && (flags & ALLOC_FLAG_ZERO)) memset(ptr, 0, PAGE_SIZE << order);
    return ptr;
}

void UnifiedAllocator::free_pages(void *ptr, u32 /*order*/) {
    AllocationGuard guard;
    buddy_allocator.free(ptr);
}

void UnifiedAllocator::set_policy(u32 new_policy_mask) {
    AllocationGuard guard;
    io.print("[UNIFIED] Changing policy from 0x%x to 0x%x\n", policy_mask, new_policy_mask);
    policy_mask = new_policy_mask;
    stats.policy_switches++;
}

void UnifiedAllocator::set_system_mode(enum system_mode mode) {
    AllocationGuard guard;
    if (mode != current_mode) {
        io.print("[UNIFIED] Switching from mode %d to mode %d\n", current_mode, mode);
        current_mode = mode;
        set_policy(mode_policy(mode));
    }
}

void UnifiedAllocator::print_stats() {
    io.print("[UNIFIED] Memory allocation statistics:\n");
    io.print("  Total allocations: %d\n", stats.total_allocations);
    io.print("  Total frees: %d\n", stats.total_frees);
    io.print("  Active allocations: %d\n", stats.active_allocations);
    io.print("  Bytes allocated: %d\n", stats.bytes_allocated);
    io.print("  Bytes freed: %d\n", stats.bytes_freed);
    
    io.print("  Allocator usage:\n");
    io.print("    Buddy: %d\n", stats.buddy_allocs);
    io.print("    Slab: %d\n", stats.slab_allocs);
    io.print("    SLOB: %d\n", stats.slob_allocs);
    io.print("    SLUB: %d\n", stats.slub_allocs);
    io.print("    Stack: %d\n", stats.stack_allocs);
    
    io.print("  Policy switches: %d\n", stats.policy_switches);
    
    if (stats.cache_hits + stats.cache_misses > 0) {
        u32 hit_rate = (stats.cache_hits * 100) / (stats.cache_hits + stats.cache_misses);
        io.print("  Cache hit rate: %d%%\n", hit_rate);
    }
    
    u32 fragmentation = calculate_fragmentation();
    io.print("  Estimated fragmentation: %d%%\n", fragmentation);
}

u32 UnifiedAllocator::calculate_fragmentation() {
    return 0;
}

void *UnifiedAllocator::stack_alloc(u32 size, u32 flags) {
    return global_stack_allocator.alloc(size, STACK_ALIGNMENT);
}

void UnifiedAllocator::stack_reset() {
    global_stack_allocator.reset();
}

void *UnifiedAllocator::stack_checkpoint() {
    return global_stack_allocator.create_checkpoint();
}

void UnifiedAllocator::stack_restore(void *checkpoint) {
    global_stack_allocator.restore_checkpoint(static_cast<struct stack_checkpoint*>(checkpoint));
}

bool UnifiedAllocator::validate_heap() {
    return true;
}

extern "C" {
void *kmalloc_flags(u32 size, u32 flags) {
    return unified_allocator.alloc(size, flags);
}

void *get_free_pages(u32 order) {
    return unified_allocator.alloc_pages(order);
}

void free_pages(void *ptr, u32 order) {
    unified_allocator.free_pages(ptr, order);
}

void set_alloc_policy(u32 policy) {
    unified_allocator.set_policy(policy);
}

void print_alloc_stats() {
    unified_allocator.print_stats();
}

void validate_kernel_heap() {
    unified_allocator.validate_heap();
}

void *kstack_alloc(u32 size) {
    return unified_allocator.stack_alloc(size);
}

void kstack_reset() {
    unified_allocator.stack_reset();
}

void *kstack_checkpoint() {
    return unified_allocator.stack_checkpoint();
}

void kstack_restore(void *checkpoint) {
    unified_allocator.stack_restore(checkpoint);
}

void *kmalloc_temp(u32 size) {
    return unified_allocator.alloc(size, ALLOC_FLAG_TEMP);
}

void *kmalloc_scoped(u32 size) {
    return unified_allocator.alloc(size, ALLOC_FLAG_SCOPED);
}
}
