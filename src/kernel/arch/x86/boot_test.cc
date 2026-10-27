#include <vmm.h>
#include <serial.h>
#include <core/boot.h>

extern "C" char kernel_end, text_start;
extern "C" u32 get_page_fault_count();
bool verify_heap();

static void finish(bool passed) {
    serial::write(passed ? "[TEST] passed\n" : "[TEST] failed\n");
    asm volatile("outl %0, %1" :: "a"(passed ? 0x10U : 0x11U), "Nd"((u16)0xf4));
    for (;;) asm volatile("cli; hlt");
}

static u32 module_hash(const multiboot_info *boot) {
    u32 hash = 2166136261U;
    if (!(boot->flags & 8)) return hash;
    const multiboot_module *modules = (const multiboot_module *)boot->mods_addr;
    for (u32 i = 0; i < boot->mods_count; ++i) {
        for (u32 address = modules[i].start; address < modules[i].end; ++address)
            hash = (hash ^ *(const u8 *)address) * 16777619U;
    }
    return hash;
}

static bool exhaust_frames(const multiboot_info *boot) {
    u32 used = vmm.frames_used;
    u32 hash = module_hash(boot);
    u32 head = 0;
    u32 count = 0;
    u32 frame;
    while ((frame = vmm.alloc_frame())) {
        if (frame < (u32)&kernel_end || (frame >= vmm.heap_start && frame < vmm.heap_start + vmm.heap_size))
            return false;
        *(u32 *)frame = head;
        head = frame;
        if (++count > vmm.frame_count) return false;
    }
    bool valid = count == vmm.frame_count - used && module_hash(boot) == hash && !vmm.alloc_frame();
    while (head) {
        frame = head;
        head = *(u32 *)head;
        vmm.free_frame(frame);
    }
    return valid && vmm.frames_used == used;
}

static bool verify_paging(const multiboot_info *boot) {
    u32 cr0, cr3;
    asm volatile("mov %%cr0, %0; mov %%cr3, %1" : "=r"(cr0), "=r"(cr3));
    if ((cr0 & 0x80010000) != 0x80010000 || cr3 != current_directory->physical_address) return false;
    if (vmm.get_physical_addr(current_directory, 0) || !vmm.verify_page_tables()) return false;

    volatile u32 *page = (volatile u32 *)KERN_HEAP;
    u32 faults = get_page_fault_count();
    if (page[0] || page[1023]) return false;
    page[0] = 0x12345678;
    u32 physical = vmm.get_physical_addr(current_directory, KERN_HEAP);
    if (!physical || *(u32 *)physical != 0x12345678 || get_page_fault_count() != faults + 1) return false;
    u32 used = vmm.frames_used;
    vmm.unmap_page(current_directory, KERN_HEAP);
    if (vmm.frames_used != used - 1 || vmm.get_physical_addr(current_directory, KERN_HEAP)) return false;
    if (page[0] || page[1023] || get_page_fault_count() != faults + 2) return false;

    u32 other = vmm.alloc_frame();
    if (!other) return false;
    *(u32 *)other = 0xaabbccdd;
    physical = vmm.get_physical_addr(current_directory, KERN_HEAP);
    if (vmm.map_page(current_directory, KERN_HEAP, other, PG_PRESENT | PG_WRITE)) return false;
    if (page[0] != 0xaabbccdd) return false;
    vmm.free_frame(physical);

    page_directory *pd = vmm.create_page_directory();
    u32 user_frame = vmm.alloc_frame();
    if (!pd || !user_frame) return false;
    *(u32 *)user_frame = 0x11223344;
    if (vmm.map_page(pd, USER_OFFSET, user_frame, PG_PRESENT | PG_WRITE | PG_USER)) return false;
    if (vmm.map_page(pd, (u32)&text_start, user_frame, PG_PRESENT | PG_USER) == 0) return false;
    vmm.switch_page_directory(pd);
    if (page[0] != 0xaabbccdd || *(volatile u32 *)USER_OFFSET != 0x11223344) return false;
    page[1] = 0x55667788;
    vmm.switch_page_directory(kernel_directory);
    vmm.destroy_page_directory(pd);
    if (page[1] != 0x55667788 || !vmm.verify_page_tables()) return false;
    vmm.unmap_page(kernel_directory, KERN_HEAP);
    return exhaust_frames(boot);
}

void run_boot_test(const multiboot_info *boot) {
    if (!(boot->flags & 4) || !boot->cmdline || boot->cmdline > USER_OFFSET - 4096) return;
    const char *args = (const char *)boot->cmdline;
    if (boot_has_argument(args, "test=heap")) finish(verify_heap());
    if (boot_has_argument(args, "test=paging")) {
        if ((boot->flags & 8) && boot->mods_count) {
            serial::write("[TEST] module hash=");
            serial::hex(module_hash(boot));
            serial::write("\n");
        }
        finish(verify_paging(boot));
    }
    if (boot_has_argument(args, "test=null")) {
        asm volatile("movl (%0), %%eax" :: "r"(0U) : "eax", "memory");
        finish(false);
    }
    if (boot_has_argument(args, "test=readonly") || boot_has_argument(args, "test=user-readonly")) {
        u32 address = boot_has_argument(args, "test=readonly") ? KERN_HEAP : USER_OFFSET;
        u32 frame = vmm.alloc_frame();
        if (!frame || vmm.map_page(current_directory, address, frame,
            PG_PRESENT | (address == USER_OFFSET ? PG_USER : 0))) finish(false);
        *(volatile u32 *)address = 1;
        finish(false);
    }
    if (boot_has_argument(args, "test=text")) {
        *(volatile u8 *)&text_start = 0;
        finish(false);
    }
    if (boot_has_argument(args, "test=invalid-opcode")) {
        asm volatile("ud2");
        finish(false);
    }
    if (boot_has_argument(args, "test=outside-heap")) {
        *(volatile u32 *)KERN_HEAP_LIM = 1;
        finish(false);
    }
    if (boot_has_argument(args, "test=out-of-memory")) {
        while (vmm.alloc_frame()) {}
        *(volatile u32 *)KERN_HEAP = 1;
        finish(false);
    }
}
