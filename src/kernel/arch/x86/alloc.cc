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
    void *ksbrk(int n) {
        struct kmalloc_header *chunk;

        if ((kern_heap + (n * PAGESIZE)) > (char *) KERN_HEAP_LIM) {
            io.print("PANIC: ksbrk(): no virtual memory left for kernel heap!\n");
            return (char *) - 1;
        }

        chunk = (struct kmalloc_header *) kern_heap;
        kern_heap += (n * PAGESIZE);
        chunk->size = PAGESIZE * n;
        chunk->used = 0;

        return chunk;
    }

    void *kmalloc(unsigned long size) {
        if (size == 0) return 0;
        
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
