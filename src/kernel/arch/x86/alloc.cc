#include <os.h>
#include <io.h>
#include <x86.h>
#include <runtime/unified_alloc.h>

struct kmalloc_header {
    unsigned long size;
    int used;
};

#define KMALLOC_MINSIZE 16

char *kern_heap = 0;
char *heap_start = 0;
unsigned long kmalloc_used = 0;
static int heap_initialized = 0;

extern "C" {
    char *get_page_frame();
    void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags);
}

    void init_heap() {
        struct kmalloc_header *initial_chunk;
        
        if (heap_initialized) return;
        
        heap_start = (char*)KERN_HEAP;
        kern_heap = (char*)KERN_HEAP;
        
        initial_chunk = (struct kmalloc_header *)heap_start;
        initial_chunk->size = PAGESIZE;
        initial_chunk->used = 0;
        
        kern_heap += PAGESIZE;
        heap_initialized = 1;
        
        io.print("[HEAP] Heap initialized at %x\n", (unsigned int)heap_start);
    }

extern "C" {
    void *ksbrk(int /*n*/) {
        return unified_allocator.alloc_pages(0);
    }

    void *kmalloc(u32 size) {
        return unified_allocator.alloc(size, ALLOC_FLAG_KERNEL);
    }

    void kfree(void *ptr) {
        unified_allocator.free(ptr);
    }
    
    void *krealloc(void *ptr, u32 new_size) {
        return unified_allocator.realloc(ptr, new_size);
    }
    
    void *kcalloc(u32 count, u32 size) {
        return unified_allocator.calloc(count, size);
    }
    
    void init_unified_allocator(int mode) {
        unified_allocator.init((enum system_mode)mode);
    }
}
