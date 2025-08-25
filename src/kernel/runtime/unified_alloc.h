#ifndef UNIFIED_ALLOC_H
#define UNIFIED_ALLOC_H

#include <runtime/types.h>
#include <runtime/buddy.h>
#include <runtime/slab.h>
#include <runtime/slob.h>
#include <runtime/slub.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define ALLOC_POLICY_BUDDY  0x01
#define ALLOC_POLICY_SLAB   0x02
#define ALLOC_POLICY_SLOB   0x04
#define ALLOC_POLICY_SLUB   0x08
#define ALLOC_POLICY_AUTO   0x10

#define ALLOC_FLAG_KERNEL   0x01
#define ALLOC_FLAG_USER     0x02
#define ALLOC_FLAG_DMA      0x04
#define ALLOC_FLAG_ATOMIC   0x08
#define ALLOC_FLAG_ZERO     0x10

enum alloc_type {
    ALLOC_TINY = 0,      // < 64 bytes
    ALLOC_SMALL = 1,     // 64-512 bytes
    ALLOC_MEDIUM = 2,    // 512-4096 bytes  
    ALLOC_LARGE = 3,     // 4KB-64KB
    ALLOC_HUGE = 4       // > 64KB
};

enum system_mode {
    SYS_MODE_EMBEDDED = 0,    // Low memory, optimize for space
    SYS_MODE_DESKTOP = 1,     // Balanced performance/memory
    SYS_MODE_SERVER = 2,      // High performance, SMP optimized
    SYS_MODE_REALTIME = 3     // Deterministic allocation times
};

struct alloc_stats {
    u32 total_allocations;
    u32 total_frees;
    u32 active_allocations;
    u32 bytes_allocated;
    u32 bytes_freed;
    u32 fragmentation_percent;
    
    u32 buddy_allocs;
    u32 slab_allocs;
    u32 slob_allocs;
    u32 slub_allocs;
    
    u32 cache_hits;
    u32 cache_misses;
    u32 policy_switches;
};

struct allocation_metadata {
    u32 size;
    u32 flags;
    u32 allocator_used;
    u32 timestamp;
    void *original_ptr;
};

class UnifiedAllocator {
public:
    void init(enum system_mode mode);
    void *alloc(u32 size, u32 flags = 0);
    void free(void *ptr);
    void *realloc(void *ptr, u32 new_size);
    void *calloc(u32 count, u32 size);
    
    void set_policy(u32 policy_mask);
    void set_system_mode(enum system_mode mode);
    void optimize_for_workload();
    void print_stats();
    void reset_stats();
    
    void *alloc_pages(u32 order, u32 flags = 0);
    void free_pages(void *ptr, u32 order);
    
    void enable_debug_tracking();
    void disable_debug_tracking();
    bool validate_heap();
    void dump_allocations();
    
private:
    enum system_mode current_mode;
    u32 policy_mask;
    bool debug_tracking;
    struct alloc_stats stats;
    
    struct allocation_metadata *debug_table;
    u32 debug_table_size;
    u32 debug_entries;
    
    enum alloc_type classify_allocation(u32 size);
    u32 select_allocator(u32 size, u32 flags);
    void update_stats(u32 size, u32 allocator, bool is_alloc);
    void track_allocation(void *ptr, u32 size, u32 flags, u32 allocator);
    void untrack_allocation(void *ptr);
    struct allocation_metadata *find_allocation(void *ptr);
    bool should_switch_policy();
    void adjust_policy_for_workload();
    u32 calculate_fragmentation();
    void *internal_alloc(u32 size, u32 flags, u32 preferred_allocator);
    void internal_free(void *ptr, u32 allocator_hint);
};

extern UnifiedAllocator unified_allocator;

extern "C" {
    void *kmalloc_flags(u32 size, u32 flags);
    void *get_free_pages(u32 order);
    void free_pages(void *ptr, u32 order);
    void set_alloc_policy(u32 policy);
    void print_alloc_stats();
    void validate_kernel_heap();
}

inline enum alloc_type get_alloc_type(u32 size) {
    if (size < 64) return ALLOC_TINY;
    if (size < 512) return ALLOC_SMALL;  
    if (size < 4096) return ALLOC_MEDIUM;
    if (size < 65536) return ALLOC_LARGE;
    return ALLOC_HUGE;
}

inline bool is_power_of_two(u32 n) {
    return n && !(n & (n - 1));
}

inline u32 next_power_of_two(u32 n) {
    if (is_power_of_two(n)) return n;
    
    u32 power = 1;
    while (power < n) power <<= 1;
    return power;
}

#endif