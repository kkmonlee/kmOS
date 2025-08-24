#include <os.h>
#include <io.h>
#include <x86.h>

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

    // Initialize the heap with a simple static buffer
    void init_heap() {
        struct kmalloc_header *initial_chunk;
        
        if (heap_initialized) return;
        
        // For simplicity, use a static buffer for the heap
        // In a real kernel, this would be proper virtual memory
        static char heap_buffer[0x100000]; // 1MB heap buffer
        
        // Set up heap pointers
        heap_start = heap_buffer;
        kern_heap = heap_buffer + sizeof(heap_buffer);
        
        // Initialize the first chunk at the beginning of our static heap
        initial_chunk = (struct kmalloc_header *)heap_start;
        initial_chunk->size = sizeof(heap_buffer);
        initial_chunk->used = 0;
        
        heap_initialized = 1;
        
        io.print("[HEAP] Heap initialized at %x with %d bytes\n", 
                 (unsigned int)heap_buffer, (int)sizeof(heap_buffer));
    }

extern "C" {
    // Change size of a memory segment
    void *ksbrk(int n) {
        struct kmalloc_header *chunk;
        char *p_addr;
        int i;

        if ((kern_heap + (n * PAGESIZE)) > (char *) KERN_HEAP_LIM) {
            io.print("PANIC: ksbrk(): no virtual memory left for kernel heap!\n");
            return (char *) - 1;
        }

        chunk = (struct kmalloc_header *) kern_heap;

        for (i = 0; i < n; i++) {
            p_addr = get_page_frame();
            if ((int)(p_addr) < 0) {
                io.print("PANIC: ksbrk(): no free page frame available!\n");
                return (char *) - 1;
            }

            pd0_add_page(kern_heap, p_addr, 0);

            kern_heap += PAGESIZE;
        }

        chunk->size = PAGESIZE * n;
        chunk->used = 0;

        return chunk;
    }

    // Allocate memory block
    void *kmalloc(unsigned long size) {
        if (size == 0) return 0;
        
        // Initialize heap if not done yet
        if (!heap_initialized) {
            init_heap();
        }

        unsigned long realsize;
        struct kmalloc_header *chunk, *other;

        if ((realsize = sizeof(struct kmalloc_header) + size) < KMALLOC_MINSIZE)
            realsize = KMALLOC_MINSIZE;

        chunk = (struct kmalloc_header *) heap_start;
        while (chunk->used || chunk->size < realsize) {
            if (chunk->size == 0) {
                io.print("\nPRINT: kmalloc(): corrupted chunk on %x with null size (heap %x)!\nSystem halted\n",
                        chunk, kern_heap);
                while(1);
                return 0;
            }

            chunk = (struct kmalloc_header *) ((char *) chunk + chunk->size);

            if (chunk >= (struct kmalloc_header *) kern_heap) {
                io.print("\nPANIC: kmalloc(): out of heap memory (requested %lu bytes)!\nSystem halted\n", size);
                while(1);
                return 0;
            }
        }

        if (chunk->size - realsize < KMALLOC_MINSIZE)
            chunk->used = 1;
        else {
            other = (struct kmalloc_header *) ((char *) chunk + realsize);
            other->size = chunk->size - realsize;
            other->used = 0;

            chunk->size = realsize;
            chunk->used = 1;
        }

        kmalloc_used += realsize;

        return (char *) chunk + sizeof(struct kmalloc_header);
    }

    // free memory block
    void kfree(void *v_addr) {
        if (v_addr == (void*)0)
            return;

        struct kmalloc_header *chunk, *other;

        chunk = (struct kmalloc_header *) ((u32)v_addr - sizeof(struct kmalloc_header));
        chunk->used = 0;

        kmalloc_used -= chunk->size;

        while ((other = (struct kmalloc_header *) ((char *) chunk + chunk->size)) 
                && other < (struct kmalloc_header *) kern_heap
                && other->used == 0)
            chunk->size += other->size;
    }
}
