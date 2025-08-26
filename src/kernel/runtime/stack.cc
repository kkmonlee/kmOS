#include <runtime/stack.h>
#include <runtime/alloc.h>
#include <runtime/os.h>

extern "C" {
    void *memset(void *s, int c, int n);
}

#include <arch/x86/io.h>
extern IO io;

StackAllocator global_stack_allocator;
thread_local StackAllocator *ThreadLocalStackAllocator::instance = nullptr;

void StackAllocator::init(u32 initial_size) {
    if (initialized) {
        return;
    }
    
    if (initial_size < MIN_STACK_SIZE) {
        initial_size = MIN_STACK_SIZE;
    }
    if (initial_size > MAX_STACK_SIZE) {
        initial_size = MAX_STACK_SIZE;
    }
    
    current_frame = allocate_frame(initial_size);
    if (!current_frame) {
        return;
    }
    
    frame_list = current_frame;
    checkpoint_stack = nullptr;
    total_capacity = initial_size;
    frame_count = 1;
    overflow_detection = true;
    underflow_detection = true;
    initialized = true;
    
    memset(&stats, 0, sizeof(stats));
}

void StackAllocator::destroy() {
    if (!initialized) {
        return;
    }
    
    while (checkpoint_stack) {
        struct stack_checkpoint *next = checkpoint_stack->prev;
        kfree(checkpoint_stack);
        checkpoint_stack = next;
    }
    
    struct stack_frame *frame = frame_list;
    while (frame) {
        struct stack_frame *next = frame->next;
        deallocate_frame(frame);
        frame = next;
    }
    
    current_frame = nullptr;
    frame_list = nullptr;
    checkpoint_stack = nullptr;
    total_capacity = 0;
    frame_count = 0;
    initialized = false;
}

void *StackAllocator::alloc(u32 size, u32 alignment) {
    if (!initialized || size == 0) {
        return nullptr;
    }
    
    if (overflow_detection) {
        stats.overflow_checks++;
    }
    
    u32 aligned_size = align_size(size, alignment);
    void *aligned_current = align_pointer(current_frame->current, alignment);
    
    if (static_cast<u8*>(aligned_current) + aligned_size > static_cast<u8*>(current_frame->end)) {
        if (!grow(aligned_size * 2)) {
            return nullptr;
        }
        aligned_current = align_pointer(current_frame->current, alignment);
        
        if (static_cast<u8*>(aligned_current) + aligned_size > static_cast<u8*>(current_frame->end)) {
            return nullptr;
        }
    }
    
    current_frame->current = static_cast<u8*>(aligned_current) + aligned_size;
    
    stats.total_allocations++;
    stats.current_usage += aligned_size;
    stats.total_allocated_bytes += aligned_size;
    update_peak_usage();
    
    if (overflow_detection && current_frame->canary != STACK_CANARY) {
        detect_corruption();
        return nullptr;
    }
    
    return aligned_current;
}

void *StackAllocator::alloc_aligned(u32 size, u32 alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = STACK_ALIGNMENT;
    }
    return alloc(size, alignment);
}

void StackAllocator::free_to_checkpoint(struct stack_checkpoint *checkpoint) {
    if (!initialized || !checkpoint) {
        return;
    }
    
    if (!is_valid_ptr(checkpoint->saved_pos)) {
        return;
    }
    
    u32 freed_bytes = static_cast<u8*>(current_frame->current) - static_cast<u8*>(checkpoint->saved_pos);
    current_frame->current = checkpoint->saved_pos;
    
    stats.current_usage = (stats.current_usage > freed_bytes) ? stats.current_usage - freed_bytes : 0;
    stats.total_freed_bytes += freed_bytes;
    
    if (underflow_detection) {
        stats.underflow_checks++;
    }
}

void StackAllocator::reset() {
    if (!initialized) {
        return;
    }
    
    current_frame->current = current_frame->start;
    stats.total_freed_bytes += stats.current_usage;
    stats.current_usage = 0;
    
    while (checkpoint_stack) {
        struct stack_checkpoint *next = checkpoint_stack->prev;
        kfree(checkpoint_stack);
        checkpoint_stack = next;
    }
}

struct stack_checkpoint *StackAllocator::create_checkpoint() {
    if (!initialized) {
        return nullptr;
    }
    
    struct stack_checkpoint *cp = static_cast<struct stack_checkpoint*>(kmalloc(sizeof(struct stack_checkpoint)));
    if (!cp) {
        return nullptr;
    }
    
    cp->saved_pos = current_frame->current;
    cp->timestamp = 0;
    cp->prev = checkpoint_stack;
    checkpoint_stack = cp;
    
    stats.checkpoint_count++;
    return cp;
}

void StackAllocator::restore_checkpoint(struct stack_checkpoint *checkpoint) {
    if (!checkpoint) {
        return;
    }
    
    free_to_checkpoint(checkpoint);
    
    struct stack_checkpoint *cp = checkpoint_stack;
    while (cp && cp != checkpoint) {
        struct stack_checkpoint *prev = cp->prev;
        kfree(cp);
        cp = prev;
        stats.checkpoint_count = (stats.checkpoint_count > 0) ? stats.checkpoint_count - 1 : 0;
    }
    
    if (cp == checkpoint) {
        checkpoint_stack = cp->prev;
        stats.checkpoint_count = (stats.checkpoint_count > 0) ? stats.checkpoint_count - 1 : 0;
    }
}

void StackAllocator::destroy_checkpoint(struct stack_checkpoint *checkpoint) {
    if (!checkpoint) {
        return;
    }
    
    struct stack_checkpoint *cp = checkpoint_stack;
    struct stack_checkpoint *prev_cp = nullptr;
    
    while (cp && cp != checkpoint) {
        prev_cp = cp;
        cp = cp->prev;
    }
    
    if (cp == checkpoint) {
        if (prev_cp) {
            prev_cp->prev = cp->prev;
        } else {
            checkpoint_stack = cp->prev;
        }
        kfree(cp);
        stats.checkpoint_count = (stats.checkpoint_count > 0) ? stats.checkpoint_count - 1 : 0;
    }
}

void *StackAllocator::get_top() const {
    return initialized ? current_frame->current : nullptr;
}

u32 StackAllocator::get_used_bytes() const {
    if (!initialized) {
        return 0;
    }
    return static_cast<u8*>(current_frame->current) - static_cast<u8*>(current_frame->start);
}

u32 StackAllocator::get_free_bytes() const {
    if (!initialized) {
        return 0;
    }
    return static_cast<u8*>(current_frame->end) - static_cast<u8*>(current_frame->current);
}

u32 StackAllocator::get_total_bytes() const {
    return initialized ? current_frame->size : 0;
}

f32 StackAllocator::get_usage_percent() const {
    if (!initialized || current_frame->size == 0) {
        return 0.0f;
    }
    return static_cast<f32>(get_used_bytes()) / current_frame->size * 100.0f;
}

bool StackAllocator::is_valid_ptr(void *ptr) const {
    if (!initialized || !ptr) {
        return false;
    }
    
    return ptr >= current_frame->start && ptr <= current_frame->current;
}

bool StackAllocator::check_integrity() const {
    if (!initialized) {
        return false;
    }
    
    return validate_frame(current_frame);
}

void StackAllocator::print_stats() const {
    if (!initialized) {
        return;
    }
    
    io.print("Stack Allocator Statistics:\n");
    io.print("  Total allocations: %u\n", stats.total_allocations);
    io.print("  Peak usage: %u bytes\n", stats.peak_usage);
    io.print("  Current usage: %u bytes\n", stats.current_usage);
    io.print("  Frame count: %u\n", frame_count);
    io.print("  Checkpoint count: %u\n", stats.checkpoint_count);
    io.print("  Usage: %.2f%%\n", get_usage_percent());
    io.print("  Corruption checks: %u\n", stats.corruption_detected);
    io.print("  Total allocated: %llu bytes\n", stats.total_allocated_bytes);
    io.print("  Total freed: %llu bytes\n", stats.total_freed_bytes);
}

void StackAllocator::reset_stats() {
    memset(&stats, 0, sizeof(stats));
}

void StackAllocator::enable_overflow_detection() {
    overflow_detection = true;
}

void StackAllocator::disable_overflow_detection() {
    overflow_detection = false;
}

void StackAllocator::enable_underflow_detection() {
    underflow_detection = true;
}

void StackAllocator::disable_underflow_detection() {
    underflow_detection = false;
}

bool StackAllocator::grow(u32 additional_size) {
    if (!initialized) {
        return false;
    }
    
    u32 new_frame_size = additional_size;
    if (new_frame_size < current_frame->size * 2) {
        new_frame_size = current_frame->size * 2;
    }
    
    if (total_capacity + new_frame_size > MAX_STACK_SIZE) {
        return false;
    }
    
    struct stack_frame *new_frame = allocate_frame(new_frame_size);
    if (!new_frame) {
        return false;
    }
    
    link_frame(new_frame);
    current_frame = new_frame;
    total_capacity += new_frame_size;
    frame_count++;
    
    return true;
}

bool StackAllocator::shrink_to_fit() {
    if (!initialized || frame_count <= 1) {
        return false;
    }
    
    while (frame_count > 1 && get_used_bytes() == 0) {
        struct stack_frame *prev_frame = current_frame->prev;
        if (!prev_frame) {
            break;
        }
        
        unlink_frame(current_frame);
        total_capacity -= current_frame->size;
        deallocate_frame(current_frame);
        current_frame = prev_frame;
        frame_count--;
    }
    
    return true;
}

struct stack_frame *StackAllocator::allocate_frame(u32 size) {
    u32 total_size = sizeof(struct stack_frame) + size;
    void *memory = kmalloc(total_size);
    if (!memory) {
        return nullptr;
    }
    
    struct stack_frame *frame = static_cast<struct stack_frame*>(memory);
    frame->start = static_cast<u8*>(memory) + sizeof(struct stack_frame);
    frame->current = frame->start;
    frame->end = static_cast<u8*>(frame->start) + size;
    frame->size = size;
    frame->magic = STACK_MAGIC;
    frame->canary = STACK_CANARY;
    frame->next = nullptr;
    frame->prev = nullptr;
    
    return frame;
}

void StackAllocator::deallocate_frame(struct stack_frame *frame) {
    if (!frame) {
        return;
    }
    
    frame->magic = 0;
    frame->canary = 0;
    kfree(frame);
}

bool StackAllocator::validate_frame(struct stack_frame *frame) const {
    if (!frame) {
        return false;
    }
    
    if (frame->magic != STACK_MAGIC) {
        return false;
    }
    
    if (frame->canary != STACK_CANARY) {
        return false;
    }
    
    if (frame->current < frame->start || frame->current > frame->end) {
        return false;
    }
    
    return true;
}

void StackAllocator::link_frame(struct stack_frame *frame) {
    if (!frame) {
        return;
    }
    
    frame->next = nullptr;
    frame->prev = current_frame;
    
    if (current_frame) {
        current_frame->next = frame;
    }
    
    if (!frame_list) {
        frame_list = frame;
    }
}

void StackAllocator::unlink_frame(struct stack_frame *frame) {
    if (!frame) {
        return;
    }
    
    if (frame->prev) {
        frame->prev->next = frame->next;
    } else {
        frame_list = frame->next;
    }
    
    if (frame->next) {
        frame->next->prev = frame->prev;
    }
    
    frame->next = nullptr;
    frame->prev = nullptr;
}

void *StackAllocator::align_pointer(void *ptr, u32 alignment) const {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return reinterpret_cast<void*>((addr + alignment - 1) & ~(alignment - 1));
}

u32 StackAllocator::align_size(u32 size, u32 alignment) const {
    return (size + alignment - 1) & ~(alignment - 1);
}

void StackAllocator::update_peak_usage() {
    if (stats.current_usage > stats.peak_usage) {
        stats.peak_usage = stats.current_usage;
    }
}

void StackAllocator::detect_corruption() {
    stats.corruption_detected++;
    
    io.print("Stack corruption detected!\n");
    io.print("Frame magic: 0x%x (expected 0x%x)\n", current_frame->magic, STACK_MAGIC);
    io.print("Frame canary: 0x%x (expected 0x%x)\n", current_frame->canary, STACK_CANARY);
}

ScopedStackAllocator::ScopedStackAllocator(StackAllocator *allocator)
    : allocator(allocator), checkpoint(nullptr), active(false) {
    if (allocator) {
        checkpoint = allocator->create_checkpoint();
        active = (checkpoint != nullptr);
    }
}

ScopedStackAllocator::~ScopedStackAllocator() {
    if (active && allocator && checkpoint) {
        allocator->restore_checkpoint(checkpoint);
        allocator->destroy_checkpoint(checkpoint);
    }
}

void *ScopedStackAllocator::alloc(u32 size, u32 alignment) {
    if (!active || !allocator) {
        return nullptr;
    }
    return allocator->alloc(size, alignment);
}

void *ScopedStackAllocator::alloc_aligned(u32 size, u32 alignment) {
    if (!active || !allocator) {
        return nullptr;
    }
    return allocator->alloc_aligned(size, alignment);
}

void ThreadLocalStackAllocator::init_thread_local() {
    if (!instance) {
        instance = static_cast<StackAllocator*>(kmalloc(sizeof(StackAllocator)));
        if (instance) {
            *instance = StackAllocator();
            instance->init();
        }
    }
}

void ThreadLocalStackAllocator::destroy_thread_local() {
    if (instance) {
        instance->destroy();
        instance->~StackAllocator();
        kfree(instance);
        instance = nullptr;
    }
}

StackAllocator *ThreadLocalStackAllocator::get_current() {
    if (!instance) {
        init_thread_local();
    }
    return instance;
}

void *ThreadLocalStackAllocator::alloc(u32 size, u32 alignment) {
    StackAllocator *allocator = get_current();
    return allocator ? allocator->alloc(size, alignment) : nullptr;
}

void ThreadLocalStackAllocator::reset() {
    StackAllocator *allocator = get_current();
    if (allocator) {
        allocator->reset();
    }
}

struct stack_checkpoint *ThreadLocalStackAllocator::checkpoint() {
    StackAllocator *allocator = get_current();
    return allocator ? allocator->create_checkpoint() : nullptr;
}

void ThreadLocalStackAllocator::restore(struct stack_checkpoint *cp) {
    StackAllocator *allocator = get_current();
    if (allocator) {
        allocator->restore_checkpoint(cp);
    }
}

extern "C" {
    void init_stack_allocator() {
        global_stack_allocator.init();
    }
    
    void *stack_alloc(u32 size) {
        return global_stack_allocator.alloc(size);
    }
    
    void *stack_alloc_aligned(u32 size, u32 alignment) {
        return global_stack_allocator.alloc_aligned(size, alignment);
    }
    
    void stack_reset() {
        global_stack_allocator.reset();
    }
    
    void *stack_checkpoint() {
        return global_stack_allocator.create_checkpoint();
    }
    
    void stack_restore(void *checkpoint) {
        global_stack_allocator.restore_checkpoint(static_cast<struct stack_checkpoint*>(checkpoint));
    }
    
    void stack_print_stats() {
        global_stack_allocator.print_stats();
    }
    
    void init_thread_stack_allocator() {
        ThreadLocalStackAllocator::init_thread_local();
    }
    
    void destroy_thread_stack_allocator() {
        ThreadLocalStackAllocator::destroy_thread_local();
    }
    
    void *tls_stack_alloc(u32 size) {
        return ThreadLocalStackAllocator::alloc(size);
    }
    
    void tls_stack_reset() {
        ThreadLocalStackAllocator::reset();
    }
}