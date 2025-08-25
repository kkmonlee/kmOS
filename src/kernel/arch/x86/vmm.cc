#include <os.h>
#include <vmm.h>
#include <runtime/buddy.h>
#include <runtime/slab.h>
#include <runtime/slob.h>
#include <runtime/slub.h>
#include <runtime/unified_alloc.h>
#include <cow.h>

VMM vmm;
struct page_directory *kernel_directory = 0;
struct page_directory *current_directory = 0;

// Physical memory starts at 1MB to avoid BIOS regions
#define PHYS_MEM_START 0x100000
#define FRAME_SIZE 4096
#define MAX_FRAMES 0x100000  // Support up to 4GB of RAM

void VMM::init() {
    io.print("[VMM] Initializing virtual memory manager\n");
    
    frame_bitmap = (u32*)kmalloc(MAX_FRAMES / 8);
    if (!frame_bitmap) {
        io.print("[VMM] Failed to allocate frame bitmap\n");
        return;
    }
    
    for (int i = 0; i < MAX_FRAMES / 32; i++) {
        frame_bitmap[i] = 0;
    }
    
    for (u32 i = 0; i < PHYS_MEM_START / FRAME_SIZE / 32; i++) {
        frame_bitmap[i] = 0xFFFFFFFF;
    }
    
    frame_count = MAX_FRAMES;
    frames_used = PHYS_MEM_START / FRAME_SIZE;
    
    kernel_directory = create_page_directory();
    
    for (u32 i = 0; i < 0x400000; i += FRAME_SIZE) {
        map_page(kernel_directory, i, i, PG_PRESENT | PG_WRITE);
    }
    
    for (u32 i = KERN_HEAP; i < KERN_HEAP_LIM; i += FRAME_SIZE) {
        u32 frame = alloc_frame();
        if (frame != 0) {
            map_page(kernel_directory, i, frame, PG_PRESENT | PG_WRITE);
        }
    }
    
    current_directory = kernel_directory;
    switch_page_directory(kernel_directory);
    
    init_buddy_allocator();
    init_slab_allocator();
    init_slob_allocator();
    init_slub_allocator();
    init_stack_allocator();
    init_unified_allocator(SYS_MODE_DESKTOP);
    init_cow_manager();
    
    io.print("[VMM] Paging enabled with %d frames available\n", frame_count - frames_used);
}

struct page_directory *VMM::create_page_directory() {
    struct page_directory *pd = (struct page_directory *)kmalloc(sizeof(struct page_directory));
    if (!pd) return 0;
    
    for (int i = 0; i < 1024; i++) {
        pd->tables[i].present = 0;
        pd->tables[i].writable = 1;
        pd->tables[i].user = 0;
        pd->tables[i].frame = 0;
        pd->page_tables[i] = 0;
    }
    
    u32 phys_addr = alloc_frame();
    pd->physical_address = phys_addr;
    
    return pd;
}

u32 VMM::alloc_frame() {
    u32 frame_idx = 0;
    u32 bitmap_idx = 0;
    
    for (bitmap_idx = 0; bitmap_idx < MAX_FRAMES / 32; bitmap_idx++) {
        if (frame_bitmap[bitmap_idx] != 0xFFFFFFFF) {
            for (int bit = 0; bit < 32; bit++) {
                if (!(frame_bitmap[bitmap_idx] & (1 << bit))) {
                    frame_idx = bitmap_idx * 32 + bit;
                    frame_bitmap[bitmap_idx] |= (1 << bit);
                    frames_used++;
                    return frame_idx * FRAME_SIZE;
                }
            }
        }
    }
    
    return 0;
}

void VMM::free_frame(u32 frame_addr) {
    u32 frame_idx = frame_addr / FRAME_SIZE;
    u32 bitmap_idx = frame_idx / 32;
    u32 bit = frame_idx % 32;
    
    if (frame_idx < MAX_FRAMES) {
        frame_bitmap[bitmap_idx] &= ~(1 << bit);
        frames_used--;
    }
}

struct page_table_entry *VMM::get_page_table(struct page_directory *pd, u32 virtual_addr, int create) {
    u32 table_idx = VADDR_PD_OFFSET(virtual_addr);
    
    if (!pd->tables[table_idx].present) {
        if (!create) return 0;
        
        u32 phys_addr = alloc_frame();
        if (!phys_addr) return 0;
        
        pd->page_tables[table_idx] = (struct page_table_entry *)kmalloc(sizeof(struct page_table_entry) * 1024);
        if (!pd->page_tables[table_idx]) {
            free_frame(phys_addr);
            return 0;
        }
        
        for (int i = 0; i < 1024; i++) {
            pd->page_tables[table_idx][i].present = 0;
            pd->page_tables[table_idx][i].writable = 1;
            pd->page_tables[table_idx][i].user = 0;
            pd->page_tables[table_idx][i].frame = 0;
        }
        
        pd->tables[table_idx].present = 1;
        pd->tables[table_idx].writable = 1;
        pd->tables[table_idx].user = 0;
        pd->tables[table_idx].frame = phys_addr >> 12;
    }
    
    return pd->page_tables[table_idx];
}

int VMM::map_page(struct page_directory *pd, u32 virtual_addr, u32 physical_addr, u32 flags) {
    struct page_table_entry *table = get_page_table(pd, virtual_addr, 1);
    if (!table) return -1;
    
    u32 page_idx = VADDR_PT_OFFSET(virtual_addr);
    
    table[page_idx].present = (flags & PG_PRESENT) ? 1 : 0;
    table[page_idx].writable = (flags & PG_WRITE) ? 1 : 0;
    table[page_idx].user = (flags & PG_USER) ? 1 : 0;
    table[page_idx].frame = physical_addr >> 12;
    
    return 0;
}

void VMM::unmap_page(struct page_directory *pd, u32 virtual_addr) {
    struct page_table_entry *table = get_page_table(pd, virtual_addr, 0);
    if (!table) return;
    
    u32 page_idx = VADDR_PT_OFFSET(virtual_addr);
    if (table[page_idx].present) {
        u32 frame_addr = table[page_idx].frame << 12;
        free_frame(frame_addr);
        table[page_idx].present = 0;
    }
}

u32 VMM::get_physical_addr(struct page_directory *pd, u32 virtual_addr) {
    struct page_table_entry *table = get_page_table(pd, virtual_addr, 0);
    if (!table) return 0;
    
    u32 page_idx = VADDR_PT_OFFSET(virtual_addr);
    if (!table[page_idx].present) return 0;
    
    return (table[page_idx].frame << 12) + VADDR_PG_OFFSET(virtual_addr);
}

int VMM::handle_page_fault(u32 fault_addr, u32 error_code) {
    if (fault_addr >= USER_OFFSET && fault_addr < USER_STACK) {
        int cow_result = cow_handle_page_fault(fault_addr, error_code);
        if (cow_result == 0) {
            return 0;
        }
    }
    
    if (fault_addr >= KERN_HEAP && fault_addr < KERN_HEAP_LIM) {
        u32 page_addr = fault_addr & ~0xFFF;
        u32 frame = alloc_frame();
        if (frame == 0) {
            io.print("[VMM] Page fault: Out of memory for kernel heap\n");
            return -1;
        }
        
        if (map_page(current_directory, page_addr, frame, PG_PRESENT | PG_WRITE) != 0) {
            io.print("[VMM] Page fault: Failed to map page\n");
            free_frame(frame);
            return -1;
        }
        
        return 0;
    }
    
    io.print("[VMM] Page fault at %x, error %x\n", fault_addr, error_code);
    return -1;
}

void VMM::switch_page_directory(struct page_directory *pd) {
    current_directory = pd;
    switch_page_directory_asm(pd->physical_address);
}

void VMM::destroy_page_directory(struct page_directory *pd) {
    if (!pd) return;
    
    for (int i = 0; i < 1024; i++) {
        if (pd->tables[i].present && pd->page_tables[i]) {
            for (int j = 0; j < 1024; j++) {
                if (pd->page_tables[i][j].present) {
                    u32 frame_addr = pd->page_tables[i][j].frame << 12;
                    free_frame(frame_addr);
                }
            }
            kfree(pd->page_tables[i]);
        }
    }
    
    if (pd->physical_address) {
        free_frame(pd->physical_address);
    }
    
    kfree(pd);
}

void init_vmm() {
    vmm.init();
}

char *get_page_frame() {
    u32 frame = vmm.alloc_frame();
    return (char*)frame;
}

extern "C" {
void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags) {
    if (kernel_directory) {
        vmm.map_page(kernel_directory, (u32)v_addr, (u32)p_addr, flags);
    }
}

void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd) {
    if (pd) {
        vmm.map_page(pd, (u32)v_addr, (u32)p_addr, flags);
    }
}

void page_fault_handler(regs_t *regs) {
    u32 fault_addr;
    asm volatile("mov %%cr2, %0" : "=r" (fault_addr));
    
    int result = vmm.handle_page_fault(fault_addr, regs->err_code);
    if (result != 0) {
        io.print("[PANIC] Unhandled page fault at %x\n", fault_addr);
        while(1);
    }
}

void enable_paging() {
    u32 cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= PAGING_FLAG;
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}

void switch_page_directory_asm(u32 physical_addr) {
    asm volatile("mov %0, %%cr3" :: "r"(physical_addr));
}
}