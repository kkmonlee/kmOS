#include <arch/x86/physical_memory.h>
#include <core/boot.h>

extern "C" int puts(const char *);
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static PhysicalMemory memory;

int main() {
    memory.reset();
    CHECK(memory.allocate() == 0);
    memory.add_usable(0, 16 * 4096);
    memory.reserve(0, 4097);
    memory.reserve(5 * 4096 + 1, 1);
    CHECK(memory.free_pages() == 13);
    CHECK(memory.allocate(4) == 6 * 4096);
    CHECK(memory.allocate(3) == 2 * 4096);
    CHECK(memory.allocate(7) == 0);
    memory.release(0);
    memory.release(4096);
    memory.release(5 * 4096);
    memory.release(2 * 4096 + 1);
    CHECK(memory.free_pages() == 6);
    memory.release(2 * 4096);
    memory.release(2 * 4096);
    CHECK(memory.free_pages() == 7);
    CHECK(memory.allocate() == 2 * 4096);
    CHECK(memory.take_heap(6) == 10 * 4096);
    memory.release(10 * 4096);
    CHECK(memory.free_pages() == 0);
    CHECK(memory.allocate() == 0);

    memory.reset();
    memory.add_usable(4097, 8190);
    CHECK(memory.free_pages() == 0);
    memory.add_usable(4097, 8191);
    CHECK(memory.free_pages() == 1);
    CHECK(memory.allocate() == 8192);

    memory.reset();
    memory.add_usable(PhysicalMemory::LIMIT - 4096, ~0ULL);
    memory.add_usable(PhysicalMemory::LIMIT, ~0ULL);
    CHECK(memory.free_pages() == 1);
    CHECK(memory.allocate() == PhysicalMemory::LIMIT - 4096);
    memory.release(PhysicalMemory::LIMIT);
    CHECK(memory.free_pages() == 0);

    struct {
        multiboot_mmap_entry first;
        u32 extension;
        multiboot_mmap_entry second;
    } __attribute__((packed)) entries = {
        {24, 0x100000, 0x1000000, 1}, 0, {20, 0x2000000, 0x1000, 2}
    };
    MultibootMemoryMap map(&entries, sizeof(entries));
    multiboot_mmap_entry entry;
    CHECK(map.next(entry) && entry.address == 0x100000 && entry.type == 1);
    CHECK(map.next(entry) && entry.address == 0x2000000 && entry.type == 2);
    CHECK(!map.next(entry) && map.valid());
    MultibootMemoryMap truncated(&entries, sizeof(entries) - 1);
    CHECK(truncated.next(entry));
    CHECK(!truncated.next(entry) && !truncated.valid());
    entries.first.size = ~0U;
    MultibootMemoryMap oversized(&entries, sizeof(entries));
    CHECK(!oversized.next(entry) && !oversized.valid());
    entries.first.size = 19;
    MultibootMemoryMap undersized(&entries, sizeof(entries));
    CHECK(!undersized.next(entry) && !undersized.valid());
    MultibootMemoryMap short_header(&entries, 3);
    CHECK(!short_header.next(entry) && !short_header.valid());
    MultibootMemoryMap missing(nullptr, 24);
    CHECK(!missing.next(entry) && !missing.valid());
    puts("physical memory tests passed");
}
