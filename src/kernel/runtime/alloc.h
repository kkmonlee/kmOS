#ifndef ALLOC_H
#define ALLOC_H

#include <runtime/types.h>

extern "C" {
    void *ksbrk(int);
    void *kmalloc(u32 size);
    void kfree(void *);
    void *krealloc(void *ptr, u32 new_size);
    void *kcalloc(u32 count, u32 size);
    void init_unified_allocator(int mode);
    
    void *kstack_alloc(u32 size);
    void kstack_reset();
    void *kstack_checkpoint();
    void kstack_restore(void *checkpoint);
    
    void *kmalloc_temp(u32 size);
    void *kmalloc_scoped(u32 size);
}

#endif