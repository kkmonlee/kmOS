#ifndef STACK_H
#define STACK_H

#include <runtime/types.h>
#include <runtime/list.h>

#define STACK_MAGIC 0x57ACF12U
#define DEFAULT_STACK_SIZE (64 * 1024)
#define MIN_STACK_SIZE 4096
#define MAX_STACK_SIZE (1024 * 1024)
#define STACK_ALIGNMENT 16
#define STACK_CANARY 0xDEADBEEF

struct stack_frame {
    void *start;
    void *current;
    void *end;
    u32 size;
    u32 magic;
    u32 canary;
    struct stack_frame *prev;
    struct stack_frame *next;
};

struct stack_checkpoint {
    void *saved_pos;
    u32 timestamp;
    struct stack_checkpoint *prev;
};

struct stack_stats {
    u32 total_allocations;
    u32 peak_usage;
    u32 current_usage;
    u32 frame_count;
    u32 checkpoint_count;
    u32 overflow_checks;
    u32 underflow_checks;
    u32 corruption_detected;
    u64 total_allocated_bytes;
    u64 total_freed_bytes;
};

class StackAllocator {
public:
    void init(u32 initial_size = DEFAULT_STACK_SIZE);
    void destroy();
    
    void *alloc(u32 size, u32 alignment = STACK_ALIGNMENT);
    void *alloc_aligned(u32 size, u32 alignment);
    void free_to_checkpoint(struct stack_checkpoint *checkpoint);
    void reset();
    
    struct stack_checkpoint *create_checkpoint();
    void restore_checkpoint(struct stack_checkpoint *checkpoint);
    void destroy_checkpoint(struct stack_checkpoint *checkpoint);
    
    void *get_top() const;
    u32 get_used_bytes() const;
    u32 get_free_bytes() const;
    u32 get_total_bytes() const;
    f32 get_usage_percent() const;
    
    bool is_valid_ptr(void *ptr) const;
    bool check_integrity() const;
    void print_stats() const;
    void reset_stats();
    
    void enable_overflow_detection();
    void disable_overflow_detection();
    void enable_underflow_detection(); 
    void disable_underflow_detection();
    
    bool grow(u32 additional_size);
    bool shrink_to_fit();
    
private:
    struct stack_frame *current_frame;
    struct stack_frame *frame_list;
    struct stack_checkpoint *checkpoint_stack;
    struct stack_stats stats;
    
    u32 total_capacity;
    u32 frame_count;
    bool overflow_detection;
    bool underflow_detection;
    bool initialized;
    
    struct stack_frame *allocate_frame(u32 size);
    void deallocate_frame(struct stack_frame *frame);
    bool validate_frame(struct stack_frame *frame) const;
    void link_frame(struct stack_frame *frame);
    void unlink_frame(struct stack_frame *frame);
    
    void *align_pointer(void *ptr, u32 alignment) const;
    u32 align_size(u32 size, u32 alignment) const;
    void update_peak_usage();
    void detect_corruption();
};

class ScopedStackAllocator {
public:
    explicit ScopedStackAllocator(StackAllocator *allocator);
    ~ScopedStackAllocator();
    
    void *alloc(u32 size, u32 alignment = STACK_ALIGNMENT);
    void *alloc_aligned(u32 size, u32 alignment);
    
private:
    StackAllocator *allocator;
    struct stack_checkpoint *checkpoint;
    bool active;
};

template<u32 SIZE>
class StaticStackAllocator {
public:
    StaticStackAllocator() : top(0), peak(0) {
        static_assert(SIZE >= MIN_STACK_SIZE, "Stack size too small");
        static_assert((SIZE & (STACK_ALIGNMENT - 1)) == 0, "Stack size must be aligned");
    }
    
    void *alloc(u32 size, u32 alignment = STACK_ALIGNMENT) {
        u32 aligned_size = align_size(size, alignment);
        u32 new_top = align_size(top + aligned_size, alignment);
        
        if (new_top > SIZE) {
            return nullptr;
        }
        
        void *ptr = &buffer[top];
        top = new_top;
        if (top > peak) peak = top;
        
        return ptr;
    }
    
    void reset() {
        top = 0;
    }
    
    u32 get_used() const { return top; }
    u32 get_peak() const { return peak; }
    u32 get_free() const { return SIZE - top; }
    f32 get_usage_percent() const { return (f32)top / SIZE * 100.0f; }
    
private:
    alignas(STACK_ALIGNMENT) u8 buffer[SIZE];
    u32 top;
    u32 peak;
    
    u32 align_size(u32 size, u32 alignment) const {
        return (size + alignment - 1) & ~(alignment - 1);
    }
};

class ThreadLocalStackAllocator {
public:
    static void init_thread_local();
    static void destroy_thread_local();
    static StackAllocator *get_current();
    static void *alloc(u32 size, u32 alignment = STACK_ALIGNMENT);
    static void reset();
    static struct stack_checkpoint *checkpoint();
    static void restore(struct stack_checkpoint *cp);
    
private:
    static thread_local StackAllocator *instance;
};

extern StackAllocator global_stack_allocator;

extern "C" {
    void init_stack_allocator();
    void *stack_alloc(u32 size);
    void *stack_alloc_aligned(u32 size, u32 alignment);
    void stack_reset();
    void *stack_checkpoint();
    void stack_restore(void *checkpoint);
    void stack_print_stats();
    
    void init_thread_stack_allocator();
    void destroy_thread_stack_allocator();
    void *tls_stack_alloc(u32 size);
    void tls_stack_reset();
}

#define STACK_ALLOC(type, count) \
    static_cast<type*>(global_stack_allocator.alloc(sizeof(type) * (count), alignof(type)))

#define STACK_ALLOC_ALIGNED(type, count, alignment) \
    static_cast<type*>(global_stack_allocator.alloc_aligned(sizeof(type) * (count), alignment))

#define TLS_STACK_ALLOC(type, count) \
    static_cast<type*>(ThreadLocalStackAllocator::alloc(sizeof(type) * (count), alignof(type)))

#define SCOPED_STACK(allocator) \
    ScopedStackAllocator _scoped_stack(&allocator)

#define STATIC_STACK_ALLOC(size) \
    StaticStackAllocator<size>()

inline void *align_ptr(void *ptr, u32 alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return reinterpret_cast<void*>((addr + alignment - 1) & ~(alignment - 1));
}

inline u32 align_size_up(u32 size, u32 alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

inline bool is_aligned(const void *ptr, u32 alignment) {
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

#endif