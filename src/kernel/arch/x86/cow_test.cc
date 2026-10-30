#include <os.h>
#include <cow.h>
#include <serial.h>
#include <core/process.h>

extern "C" u32 get_page_fault_count();

#define CHECK(condition) do { if (!(condition)) { \
    serial::write("[COW] failed line="); \
    serial::hex(__LINE__); \
    serial::write("\n"); \
    return false; \
} } while (0)

static u32 cpu_directory() {
    u32 value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static page_table_entry *entry(page_directory *pd, u32 address = USER_OFFSET) {
    page_table_entry *table = vmm.get_page_table(pd, address, 0);
    return table ? &table[VADDR_PT_OFFSET(address)] : nullptr;
}

static void fill(u32 address, u32 seed) {
    volatile u8 *bytes = (volatile u8 *)address;
    for (u32 i = 0; i < 4096; ++i) bytes[i] = (u8)(i * 37 + seed);
}

static bool matches(page_directory *pd, u32 address, u32 seed) {
    u32 frame = vmm.get_physical_addr(pd, address);
    if (!frame) return false;
    const volatile u8 *bytes = (const volatile u8 *)frame;
    for (u32 i = 0; i < 4096; ++i) {
        if (bytes[i] != (u8)(i * 37 + seed)) return false;
    }
    return true;
}

static u32 add_page(page_directory *pd, u32 address, u32 flags, u32 seed) {
    u32 frame = vmm.alloc_frame();
    if (!frame) return 0;
    fill(frame, seed);
    if (vmm.map_page(pd, address, frame, flags | PG_PRESENT | PG_USER)) {
        vmm.free_frame(frame);
        return 0;
    }
    return frame;
}

static void destroy(page_directory *pd) {
    if (current_directory == pd) vmm.switch_page_directory(kernel_directory);
    vmm.destroy_page_directory(pd);
}

static u32 exhaust_frames() {
    u32 head = 0;
    while (u32 frame = vmm.alloc_frame()) {
        *(u32 *)frame = head;
        head = frame;
    }
    return head;
}

static u32 release_one(u32 head) {
    u32 next = *(u32 *)head;
    vmm.free_frame(head);
    return next;
}

static void release_frames(u32 head) {
    while (head) head = release_one(head);
}

static bool write_isolation() {
    u32 before = vmm.frames_used;
    page_directory *parent = vmm.create_page_directory();
    page_directory *child = vmm.create_page_directory();
    CHECK(parent && child);
    u32 original = add_page(parent, USER_OFFSET, PG_WRITE, 17);
    CHECK(original && cow_fork_mm(child, parent) == 0);
    CHECK(current_directory == kernel_directory && cpu_directory() == kernel_directory->physical_address);
    CHECK(vmm.frame_references(original) == 2);
    CHECK(entry(parent)->cow && entry(child)->cow && !entry(parent)->writable);
    vmm.switch_page_directory(child);
    u32 faults = get_page_fault_count();
    fill(USER_OFFSET, 29);
    CHECK(get_page_fault_count() == faults + 1);
    CHECK(!entry(child)->cow && entry(child)->writable);
    CHECK(matches(child, USER_OFFSET, 29) && matches(parent, USER_OFFSET, 17));
    CHECK(vmm.frame_references(original) == 1);
    u32 probe = vmm.alloc_frame();
    CHECK(probe && probe != original);
    fill(probe, 71);
    CHECK(matches(parent, USER_OFFSET, 17));
    vmm.free_frame(probe);
    destroy(child);
    vmm.switch_page_directory(parent);
    u32 used = vmm.frames_used;
    fill(USER_OFFSET, 43);
    CHECK(vmm.frames_used == used && vmm.get_physical_addr(parent, USER_OFFSET) == original);
    CHECK(!entry(parent)->cow && entry(parent)->writable && cow_validate() == 0);
    destroy(parent);
    CHECK(vmm.frames_used == before && !vmm.frame_references(original));
    return true;
}

static bool repeated_forks() {
    u32 before = vmm.frames_used;
    page_directory *spaces[4];
    for (u32 i = 0; i < 4; ++i) {
        spaces[i] = vmm.create_page_directory();
        CHECK(spaces[i]);
    }
    u32 original = add_page(spaces[0], USER_OFFSET, PG_WRITE, 5);
    CHECK(original);
    vmm.switch_page_directory(spaces[0]);
    fill(USER_OFFSET, 11);
    CHECK(cow_fork_mm(spaces[1], spaces[0]) == 0);
    CHECK(cow_fork_mm(spaces[2], spaces[0]) == 0);
    CHECK(cow_fork_mm(spaces[3], spaces[1]) == 0);
    CHECK(vmm.frame_references(original) == 4 && cpu_directory() == spaces[0]->physical_address);
    u32 faults = get_page_fault_count();
    fill(USER_OFFSET, 19);
    CHECK(get_page_fault_count() == faults + 1 && vmm.frame_references(original) == 3);
    CHECK(matches(spaces[1], USER_OFFSET, 11) && matches(spaces[2], USER_OFFSET, 11));
    CHECK(cow_manager.handle_cow_fault(spaces[1], USER_OFFSET, 7) == 0);
    CHECK(cpu_directory() == spaces[0]->physical_address && current_directory == spaces[0]);
    CHECK(vmm.frame_references(original) == 2 && matches(spaces[1], USER_OFFSET, 11));
    destroy(spaces[0]);
    destroy(spaces[2]);
    CHECK(vmm.frame_references(original) == 1);
    vmm.switch_page_directory(spaces[3]);
    u32 used = vmm.frames_used;
    fill(USER_OFFSET, 31);
    CHECK(vmm.frames_used == used && matches(spaces[1], USER_OFFSET, 11));
    destroy(spaces[1]);
    destroy(spaces[3]);
    CHECK(vmm.frames_used == before);
    for (u32 i = 0; i < 32; ++i) {
        page_directory *parent = vmm.create_page_directory();
        page_directory *child = vmm.create_page_directory();
        CHECK(parent && child && add_page(parent, USER_OFFSET, PG_WRITE, i));
        CHECK(cow_fork_mm(child, parent) == 0);
        destroy(parent);
        CHECK(matches(child, USER_OFFSET, i));
        cow_manager.cleanup_process_cow(child);
        cow_manager.cleanup_process_cow(child);
        destroy(child);
        CHECK(vmm.frames_used == before);
    }
    return true;
}

static bool readonly_aliases() {
    u32 before = vmm.frames_used;
    page_directory *parent = vmm.create_page_directory();
    page_directory *child = vmm.create_page_directory();
    CHECK(parent && child);
    u32 frame = add_page(parent, USER_OFFSET, PG_WRITE, 53);
    CHECK(frame && vmm.retain_frame(frame));
    CHECK(!vmm.map_page(parent, USER_OFFSET + 4096, frame, PG_PRESENT | PG_USER));
    CHECK(cow_fork_mm(child, parent) == 0 && vmm.frame_references(frame) == 4);
    CHECK(!entry(parent, USER_OFFSET + 4096)->cow && !entry(child, USER_OFFSET + 4096)->cow);
    CHECK(cow_manager.handle_cow_fault(child, USER_OFFSET + 4096, 3) == -1);
    const u32 errors[] = {0, 1, 2, 11, 19, 35};
    for (u32 error : errors) {
        CHECK(cow_manager.handle_cow_fault(child, USER_OFFSET, error) == -1);
    }
    CHECK(cow_manager.handle_cow_fault(child, USER_OFFSET, 3) == 0);
    CHECK(vmm.frame_references(frame) == 3 && matches(child, USER_OFFSET + 4096, 53));
    Process *process = new Process(const_cast<char *>("cow_child"));
    CHECK(process);
    process->getPInfo()->pd = child;
    delete process;
    CHECK(vmm.frame_references(frame) == 2 && matches(parent, USER_OFFSET, 53));
    vmm.unmap_page(parent, USER_OFFSET + 4096);
    CHECK(vmm.frame_references(frame) == 1);
    vmm.switch_page_directory(parent);
    cow_optimize();
    CHECK(!entry(parent)->cow && entry(parent)->writable && cow_validate() == 0);
    destroy(parent);
    CHECK(vmm.frames_used == before);
    return true;
}

static bool allocation_failures() {
    u32 before = vmm.frames_used;
    page_directory *parent = vmm.create_page_directory();
    page_directory *child = vmm.create_page_directory();
    CHECK(parent && child);
    u32 first = add_page(parent, USER_OFFSET, PG_WRITE, 61);
    u32 second = add_page(parent, USER_OFFSET + 0x400000, PG_WRITE, 67);
    CHECK(first && second);
    u32 held = exhaust_frames();
    CHECK(held);
    held = release_one(held);
    u32 used = vmm.frames_used;
    CHECK(cow_fork_mm(child, parent) == -1 && vmm.frames_used == used);
    CHECK(!entry(parent)->cow && entry(parent)->writable && vmm.frame_references(first) == 1);
    CHECK(!entry(parent, USER_OFFSET + 0x400000)->cow && vmm.frame_references(second) == 1);
    CHECK(!child->tables[VADDR_PD_OFFSET(USER_OFFSET)].present);
    CHECK(!child->tables[VADDR_PD_OFFSET(USER_OFFSET + 0x400000)].present);
    CHECK(matches(parent, USER_OFFSET, 61) && matches(parent, USER_OFFSET + 0x400000, 67));
    release_frames(held);
    page_table_entry *existing = vmm.get_page_table(child, USER_OFFSET, 1);
    CHECK(existing && !child->tables[VADDR_PD_OFFSET(USER_OFFSET)].user);
    held = exhaust_frames();
    used = vmm.frames_used;
    CHECK(cow_fork_mm(child, parent) == -1 && vmm.frames_used == used);
    CHECK(vmm.get_page_table(child, USER_OFFSET, 0) == existing);
    CHECK(!existing[0].present && !child->tables[VADDR_PD_OFFSET(USER_OFFSET)].user);
    CHECK(entry(parent)->writable && !entry(parent)->cow && vmm.frame_references(first) == 1);
    release_frames(held);
    CHECK(cow_fork_mm(child, parent) == 0);
    held = exhaust_frames();
    used = vmm.frames_used;
    CHECK(cow_manager.handle_cow_fault(child, USER_OFFSET, 3) == -1);
    CHECK(vmm.frames_used == used && vmm.frame_references(first) == 2);
    CHECK(entry(child)->cow && matches(child, USER_OFFSET, 61));
    held = release_one(held);
    CHECK(cow_manager.handle_cow_fault(child, USER_OFFSET, 3) == 0);
    CHECK(vmm.frame_references(first) == 1 && matches(parent, USER_OFFSET, 61));
    release_frames(held);
    destroy(parent);
    destroy(child);
    CHECK(vmm.frames_used == before);
    return true;
}

static bool ranges_and_cleanup() {
    u32 before = vmm.frames_used;
    page_directory *parent = vmm.create_page_directory();
    page_directory *child = vmm.create_page_directory();
    CHECK(parent && child && add_page(parent, USER_OFFSET, PG_WRITE, 79));
    u32 original = vmm.get_physical_addr(parent, USER_OFFSET);
    CHECK(vmm.map_page(child, USER_OFFSET, original, PG_PRESENT | PG_USER | PG_COW | PG_WRITE) == -1);
    CHECK(vmm.map_page(child, USER_OFFSET, original, PG_PRESENT | PG_COW) == -1);
    CHECK(vmm.map_page(child, USER_OFFSET, original, PG_USER | PG_COW) == -1);
    CHECK(cow_manager.cow_copy_page_range(child, parent, USER_OFFSET + 1, USER_STACK) == -1);
    CHECK(cow_manager.cow_copy_page_range(child, parent, USER_OFFSET, USER_STACK + 4096) == -1);
    CHECK(cow_manager.cow_copy_page_range(parent, parent, USER_OFFSET, USER_STACK) == -1);
    CHECK(cow_manager.cow_copy_page_range(nullptr, parent, USER_OFFSET, USER_STACK) == -1);
    CHECK(cow_manager.cow_copy_page_range(child, parent, USER_OFFSET, USER_OFFSET) == 0);
    CHECK(add_page(child, USER_OFFSET + 4096, 0, 83));
    CHECK(cow_fork_mm(child, parent) == -1 && !entry(parent)->cow);
    CHECK(matches(child, USER_OFFSET + 4096, 83));
    CHECK(cow_manager.cow_copy_page_range(child, parent, USER_OFFSET, USER_OFFSET + 4096) == 0);
    CHECK(matches(child, USER_OFFSET, 79) && matches(child, USER_OFFSET + 4096, 83));
    cow_manager.cow_free_page_range(child, USER_OFFSET, USER_OFFSET + 4096);
    CHECK(!vmm.get_physical_addr(child, USER_OFFSET) && matches(child, USER_OFFSET + 4096, 83));
    destroy(parent);
    destroy(child);
    CHECK(vmm.frames_used == before);

    page_directory *pd = vmm.create_page_directory();
    CHECK(pd);
    u32 start = USER_OFFSET + 0x3ff000;
    u32 held = exhaust_frames();
    CHECK(held);
    held = release_one(held);
    held = release_one(held);
    u32 used = vmm.frames_used;
    CHECK(cow_manager.map_pages(pd, start, start + 8192, PG_PRESENT | PG_WRITE | PG_USER) == -1);
    CHECK(vmm.frames_used == used && !vmm.get_physical_addr(pd, start));
    release_frames(held);
    CHECK(cow_manager.map_pages(pd, start, start + 8192, PG_PRESENT | PG_WRITE | PG_USER) == 0);
    vmm.switch_page_directory(pd);
    for (u32 i = 0; i < 8192; ++i) CHECK(!((volatile u8 *)start)[i]);
    CHECK(cow_manager.unmap_pages(pd, start, start + 8192) == 0);
    destroy(pd);
    CHECK(vmm.frames_used == before);
    return true;
}

bool verify_cow() {
    CHECK(write_isolation());
    CHECK(repeated_forks());
    CHECK(readonly_aliases());
    CHECK(allocation_failures());
    CHECK(ranges_and_cleanup());
    CHECK(current_directory == kernel_directory && cpu_directory() == kernel_directory->physical_address);
    serial::write("[COW] ownership, isolation, rollback and cleanup verified\n");
    return true;
}
