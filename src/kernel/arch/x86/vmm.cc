#include <os.h>
#include <vmm.h>
#include <swap.h>
#include <runtime/buddy.h>
#include <runtime/slab.h>
#include <runtime/slob.h>
#include <runtime/slub.h>
#include <runtime/unified_alloc.h>
#include <cow.h>
#include <physical_memory.h>
#include <core/boot.h>

VMM vmm;
struct page_directory *kernel_directory = 0;
struct page_directory *current_directory = 0;

extern "C" char kernel_end;
#define FRAME_SIZE 4096
static PhysicalMemory physical_memory;

static_assert(sizeof(page_directory_entry) == 4, "PDE size");
static_assert(sizeof(page_table_entry) == 4, "PTE size");
static_assert(__builtin_offsetof(page_directory, tables) == 0, "directory layout");

static void zero_memory(void *address, u32 size) {
    u8 *bytes = (u8 *)address;
    for (u32 i = 0; i < size; ++i) bytes[i] = 0;
}

static void invalidate_page(u32 address) {
    asm volatile("invlpg (%0)" :: "r"(address) : "memory");
}

static void serial_outb_vmm(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static void serial_print_vmm(const char* str) {
    while (*str) {
        serial_outb_vmm(0x3F8, *str);
        str++;
    }
}

void VMM::init(const multiboot_info *boot) {
    serial_print_vmm("[VMM] Starting VMM initialization\n");
    io.print("[VMM] Initializing virtual memory manager\n");
    
    if (!init_physical_memory(boot)) {
        serial_print_vmm("[PANIC] Invalid or insufficient boot memory\n");
        for (;;) asm volatile("cli; hlt");
    }
    serial_print_vmm("[VMM] Memory map verified\n");

    kernel_directory = create_page_directory();
    if (!kernel_directory) {
        serial_print_vmm("[PANIC] No memory for page directory\n");
        for (;;) asm volatile("cli; hlt");
    }
    for (u32 address = 0; address < frame_count * FRAME_SIZE; address += FRAME_SIZE) {
        if (map_page(kernel_directory, address, address, PG_PRESENT | PG_WRITE) != 0) {
            serial_print_vmm("[PANIC] No memory for page table\n");
            for (;;) asm volatile("cli; hlt");
        }
    }
    switch_page_directory(kernel_directory);
    if (!verify_page_tables()) {
        serial_print_vmm("[PANIC] Page table verification failed\n");
        for (;;) asm volatile("cli; hlt");
    }
    serial_print_vmm("[VMM] Page tables verified\n");

    init_buddy_allocator((void *)heap_start, heap_size);
    init_slab_allocator();
    init_slob_allocator();
    init_slub_allocator();
    init_unified_allocator(SYS_MODE_DESKTOP);
    init_stack_allocator();
    init_cow_manager();
    
    io.print("[VMM] Page tables ready with %d frames available\n", frame_count - frames_used);
}

struct page_directory *VMM::create_page_directory() {
    const u32 pages = (sizeof(page_directory) + FRAME_SIZE - 1) / FRAME_SIZE;
    u32 address = alloc_frames(pages);
    if (!address) return nullptr;
    zero_memory((void *)address, pages * FRAME_SIZE);
    page_directory *pd = (page_directory *)address;
    pd->physical_address = address;
    return pd;
}

void VMM::update_frame_counts() {
    frame_count = physical_memory.frame_count();
    frames_used = frame_count - physical_memory.free_pages();
}

u32 VMM::alloc_frames(u32 count) {
    u32 address = physical_memory.allocate(count);
    update_frame_counts();
    return address;
}

u32 VMM::alloc_frame() {
    return alloc_frames(1);
}

void VMM::free_frame(u32 address) {
    physical_memory.release(address);
    update_frame_counts();
}

bool VMM::init_physical_memory(const multiboot_info *boot) {
    if (!boot || (u32)boot > PhysicalMemory::LIMIT - sizeof(*boot)) return false;
    physical_memory.reset();
    if (boot->flags & (1U << 6)) {
        if (!boot->mmap_length || !boot->mmap_addr ||
            boot->mmap_addr >= PhysicalMemory::LIMIT ||
            boot->mmap_length > PhysicalMemory::LIMIT - boot->mmap_addr) return false;
        for (u32 pass = 0; pass < 2; ++pass) {
            MultibootMemoryMap map((const void *)boot->mmap_addr, boot->mmap_length);
            multiboot_mmap_entry entry;
            while (map.next(entry)) {
                if (pass == 0 && entry.type == 1) physical_memory.add_usable(entry.address, entry.length);
                if (pass == 1 && entry.type != 1) physical_memory.reserve(entry.address, entry.length);
            }
            if (!map.valid()) return false;
        }
        physical_memory.reserve(boot->mmap_addr, boot->mmap_length);
    } else if (boot->flags & 1U) {
        physical_memory.add_usable(0x100000, (u64)boot->high_mem * 1024);
    } else {
        return false;
    }
    physical_memory.reserve(0, (u32)&kernel_end);
    physical_memory.reserve((u32)boot, sizeof(*boot));
    if (boot->flags & (1U << 2)) physical_memory.reserve(boot->cmdline, 4096);
    if (boot->flags & (1U << 9)) physical_memory.reserve(boot->boot_loader_name, 4096);
    if (boot->flags & (1U << 3)) {
        if ((boot->mods_count && !boot->mods_addr) || boot->mods_addr >= PhysicalMemory::LIMIT ||
            boot->mods_count > (PhysicalMemory::LIMIT - boot->mods_addr) / sizeof(multiboot_module)) return false;
        const multiboot_module *modules = (const multiboot_module *)boot->mods_addr;
        physical_memory.reserve(boot->mods_addr, (u64)boot->mods_count * sizeof(*modules));
        for (u32 i = 0; i < boot->mods_count; ++i) {
            if (modules[i].end < modules[i].start) return false;
            physical_memory.reserve(modules[i].start, modules[i].end - modules[i].start);
            if (modules[i].string) physical_memory.reserve(modules[i].string, 4096);
        }
    }
    if (boot->flags & (1U << 5)) {
        physical_memory.reserve(boot->elf_sec.addr, (u64)boot->elf_sec.num * boot->elf_sec.size);
    }
    if (boot->flags & (1U << 4)) {
        physical_memory.reserve(boot->elf_sec.addr, (u64)boot->elf_sec.num + boot->elf_sec.size);
    }
    if (boot->flags & (1U << 7)) physical_memory.reserve(boot->drives_addr, boot->drives_length);
    if (boot->flags & (1U << 10)) physical_memory.reserve(boot->apm_table, 20);
    if (boot->flags & (1U << 11)) {
        physical_memory.reserve(boot->vbe_control_info, 512);
        physical_memory.reserve(boot->vbe_mode_info, 256);
    }
    u32 heap_pages = physical_memory.free_pages() / 4;
    if (heap_pages > 4096) heap_pages = 4096;
    if (heap_pages < 512) heap_pages = 512;
    heap_start = physical_memory.take_heap(heap_pages);
    heap_size = heap_pages * FRAME_SIZE;
    update_frame_counts();
    return heap_start && physical_memory.free_pages() >= 64;
}

page_table_entry *VMM::get_page_table(page_directory *pd, u32 address, int create) {
    if (!pd) return nullptr;
    u32 index = VADDR_PD_OFFSET(address);
    if (!pd->tables[index].present) {
        if (!create) return nullptr;
        u32 physical = alloc_frame();
        if (!physical) return nullptr;
        zero_memory((void *)physical, FRAME_SIZE);
        pd->page_tables[index] = (page_table_entry *)physical;
        pd->tables[index].frame = physical >> 12;
        pd->tables[index].writable = 1;
        pd->tables[index].present = 1;
    }
    return pd->page_tables[index];
}

int VMM::map_page(page_directory *pd, u32 address, u32 physical, u32 flags) {
    if ((address | physical) & (FRAME_SIZE - 1)) return -1;
    page_table_entry *table = get_page_table(pd, address, 1);
    if (!table) return -1;
    u32 index = VADDR_PT_OFFSET(address);
    zero_memory(&table[index], sizeof(table[index]));
    table[index].frame = physical >> 12;
    table[index].present = (flags & PG_PRESENT) != 0;
    table[index].writable = (flags & PG_WRITE) != 0;
    table[index].user = (flags & PG_USER) != 0;
    if (flags & PG_USER) pd->tables[VADDR_PD_OFFSET(address)].user = 1;
    if (pd == current_directory) invalidate_page(address);
    return 0;
}

void VMM::unmap_page(page_directory *pd, u32 address) {
    page_table_entry *table = get_page_table(pd, address, 0);
    if (!table) return;
    u32 index = VADDR_PT_OFFSET(address);
    if (table[index].present) {
        u32 frame = table[index].frame << 12;
        zero_memory(&table[index], sizeof(table[index]));
        if (pd == current_directory) invalidate_page(address);
        free_frame(frame);
    }
}

u32 VMM::get_physical_addr(page_directory *pd, u32 address) {
    page_table_entry *table = get_page_table(pd, address, 0);
    if (!table || !table[VADDR_PT_OFFSET(address)].present) return 0;
    return (table[VADDR_PT_OFFSET(address)].frame << 12) | VADDR_PG_OFFSET(address);
}

bool VMM::verify_page_tables() {
    u32 before = frames_used;
    page_directory *pd = create_page_directory();
    u32 frame = alloc_frame();
    if (!pd || !frame) return false;
    u32 address = USER_OFFSET;
    bool valid = (pd->physical_address == (u32)pd->tables) &&
        !(pd->physical_address & 4095) &&
        map_page(pd, address, frame, PG_PRESENT | PG_WRITE | PG_USER) == 0 &&
        get_physical_addr(pd, address + 123) == frame + 123;
    page_table_entry *table = get_page_table(pd, address, 0);
    valid = valid && table && ((u32)table == (pd->tables[VADDR_PD_OFFSET(address)].frame << 12)) &&
        pd->tables[VADDR_PD_OFFSET(address)].user &&
        map_page(pd, address + 1, frame, PG_PRESENT) != 0;
    destroy_page_directory(pd);
    return valid && frames_used == before;
}

int VMM::handle_page_fault(u32 fault_addr, u32 error_code) {
    u32 page_addr = fault_addr & ~0xFFF;
    
    u32 swap_entry = get_swap_entry(current_directory, page_addr);
    if (swap_entry != 0) {
        if (swap_manager.swap_in_page(page_addr, swap_entry) == 0) {
            remove_swapped_page(current_directory, page_addr);
            swap_manager.update_page_access(page_addr);
            return 0;
        }
    }
    
    if (fault_addr >= USER_OFFSET && fault_addr < USER_STACK) {
        int cow_result = cow_handle_page_fault(fault_addr, error_code);
        if (cow_result == 0) {
            swap_manager.update_page_access(page_addr);
            return 0;
        }
    }
    
    if (fault_addr >= KERN_HEAP && fault_addr < KERN_HEAP_LIM) {
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
        
        swap_manager.add_to_lru(page_addr);
        return 0;
    }
    
    io.print("[VMM] Page fault at %x, error %x\n", fault_addr, error_code);
    return -1;
}

void VMM::switch_page_directory(struct page_directory *pd) {
    current_directory = pd;
    switch_page_directory_asm(pd->physical_address);
}

void VMM::destroy_page_directory(page_directory *pd) {
    if (!pd || pd == kernel_directory || pd == current_directory) return;
    for (u32 i = 0; i < 1024; ++i) {
        if (!pd->tables[i].present || !pd->page_tables[i]) continue;
        for (u32 j = 0; j < 1024; ++j) {
            if (pd->page_tables[i][j].present && pd->page_tables[i][j].user) {
                free_frame(pd->page_tables[i][j].frame << 12);
            }
        }
        free_frame(pd->tables[i].frame << 12);
    }
    swapped_page_entry *swapped = pd->swapped_pages;
    while (swapped) {
        swapped_page_entry *next = swapped->next;
        swap_manager.free_swap_entry(swapped->swap_entry);
        kfree(swapped);
        swapped = next;
    }
    const u32 pages = (sizeof(page_directory) + FRAME_SIZE - 1) / FRAME_SIZE;
    u32 address = pd->physical_address;
    for (u32 i = 0; i < pages; ++i) free_frame(address + i * FRAME_SIZE);
}

int VMM::add_swapped_page(struct page_directory *pd, u32 virtual_addr, u32 swap_entry) {
    struct swapped_page_entry *entry = (struct swapped_page_entry *)kmalloc(sizeof(struct swapped_page_entry));
    if (!entry) return -1;
    
    entry->virtual_addr = virtual_addr;
    entry->swap_entry = swap_entry;
    entry->next = pd->swapped_pages;
    pd->swapped_pages = entry;
    
    return 0;
}

u32 VMM::get_swap_entry(struct page_directory *pd, u32 virtual_addr) {
    struct swapped_page_entry *entry = pd->swapped_pages;
    
    while (entry) {
        if (entry->virtual_addr == virtual_addr) {
            return entry->swap_entry;
        }
        entry = entry->next;
    }
    
    return 0;
}

void VMM::remove_swapped_page(struct page_directory *pd, u32 virtual_addr) {
    struct swapped_page_entry **entry = &pd->swapped_pages;
    
    while (*entry) {
        if ((*entry)->virtual_addr == virtual_addr) {
            struct swapped_page_entry *to_remove = *entry;
            *entry = (*entry)->next;
            kfree(to_remove);
            return;
        }
        entry = &(*entry)->next;
    }
}

int VMM::try_reclaim_memory(u32 pages_needed) {
    return swap_manager.reclaim_pages(pages_needed);
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
    asm volatile("mov %0, %%cr3" :: "r"(physical_addr) : "memory");
}
}
