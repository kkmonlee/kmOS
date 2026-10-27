#include <runtime/alloc.h>
#include <runtime/unified_alloc.h>
#include <serial.h>

#define CHECK(condition) do { if (!(condition)) { \
    serial::write("[HEAP] failed line="); \
    serial::hex(__LINE__); \
    serial::write("\n"); \
    return false; \
} } while (0)

static void fill(u8 *ptr, u32 size, u32 seed) {
    for (u32 i = 0; i < size; ++i) ptr[i] = (u8)(i * 37 + seed);
}

static bool matches(const u8 *ptr, u32 size, u32 seed) {
    for (u32 i = 0; i < size; ++i) {
        if (ptr[i] != (u8)(i * 37 + seed)) return false;
    }
    return true;
}

static bool mixed_allocations() {
    const u32 sizes[] = {1, 8, 16, 32, 63, 64, 127, 128, 255, 256,
                         479, 480, 511, 512, 1024, 2048, 4095, 4096, 8192, 65536};
    const u32 count = sizeof(sizes) / sizeof(sizes[0]);
    u8 *blocks[count];
    for (u32 round = 0; round < 16; ++round) {
        for (u32 i = 0; i < count; ++i) {
            blocks[i] = (u8 *)kmalloc(sizes[i]);
            CHECK(blocks[i] && !((u32)blocks[i] & 15));
            fill(blocks[i], sizes[i], i + round);
        }
        for (u32 i = 0; i < count; ++i) {
            u32 index = i * 7 % count;
            CHECK(matches(blocks[index], sizes[index], index + round));
            kfree(blocks[index]);
        }
    }
    return true;
}

static bool reallocation() {
    const u32 sizes[] = {32, 512, 4096, 8192};
    for (u32 size : sizes) {
        u8 *ptr = (u8 *)krealloc(nullptr, size);
        CHECK(ptr);
        fill(ptr, size, 93);
        CHECK(!krealloc(ptr, ~0U));
        CHECK(matches(ptr, size, 93));
        u32 grown_size = size * 2 + 7;
        u8 *grown = (u8 *)krealloc(ptr, grown_size);
        CHECK(grown && !((u32)grown & 15));
        CHECK(matches(grown, size, 93));
        fill(grown, grown_size, 17);
        u8 *shrunk = (u8 *)krealloc(grown, size / 2);
        CHECK(shrunk && matches(shrunk, size / 2, 17));
        CHECK(!krealloc(shrunk, 0));
    }
    u8 *zero = (u8 *)kcalloc(33, 7);
    CHECK(zero);
    for (u32 i = 0; i < 231; ++i) CHECK(!zero[i]);
    fill(zero, 231, 71);
    kfree(zero);
    return true;
}

static bool cache_reuse() {
    const u32 sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    for (u32 size : sizes) {
        u8 *first = (u8 *)slab_alloc(size);
        CHECK(first);
        fill(first, size, 5);
        slab_free(first);
        u8 *second = (u8 *)slab_alloc(size);
        CHECK(second == first);
        fill(second, size, 7);
        slab_free(second);
    }
    u8 *first = (u8 *)slub_alloc(64);
    CHECK(first);
    fill(first, 64, 11);
    slub_free(first);
    u8 *second = (u8 *)slub_alloc(64);
    CHECK(second == first);
    fill(second, 64, 13);
    slub_free(second);
    return true;
}

static bool tracking_and_modes() {
    const system_mode modes[] = {SYS_MODE_EMBEDDED, SYS_MODE_DESKTOP,
                                 SYS_MODE_SERVER, SYS_MODE_REALTIME};
    u8 *live[4];
    for (u32 i = 0; i < 4; ++i) {
        unified_allocator.set_system_mode(modes[i]);
        live[i] = (u8 *)kmalloc(127);
        CHECK(live[i]);
        fill(live[i], 127, i);
    }
    unified_allocator.enable_debug_tracking();
    unified_allocator.set_system_mode(SYS_MODE_DESKTOP);
    for (u32 i = 0; i < 4; ++i) {
        CHECK(matches(live[i], 127, i));
        kfree(live[i]);
    }

    static u8 *blocks[1536];
    for (u32 i = 0; i < 1536; ++i) {
        blocks[i] = (u8 *)kmalloc(32);
        CHECK(blocks[i]);
        fill(blocks[i], 32, i);
    }
    u8 *grown = (u8 *)krealloc(blocks[1535], 96);
    CHECK(grown && matches(grown, 32, 1535));
    fill(grown, 96, 1535);
    blocks[1535] = grown;
    unified_allocator.disable_debug_tracking();
    for (u32 i = 0; i < 1536; ++i) {
        CHECK(matches(blocks[i], 32, i));
        kfree(blocks[i]);
    }
    unified_allocator.enable_debug_tracking();
    CHECK(reallocation());
    unified_allocator.disable_debug_tracking();
    return true;
}

static bool exhaustion() {
    u32 previous_count = 0;
    for (u32 round = 0; round < 3; ++round) {
        void *head = nullptr;
        u32 count = 0;
        while (void *ptr = kmalloc(65536)) {
            *(void **)ptr = head;
            head = ptr;
            CHECK(++count < 4096);
        }
        CHECK(count && (!round || count == previous_count));
        previous_count = count;
        fill((u8 *)head + sizeof(void *), 64, 23);
        CHECK(!krealloc(head, 131072));
        CHECK(matches((u8 *)head + sizeof(void *), 64, 23));
        void *deferred = nullptr;
        while (head) {
            void *ptr = head;
            head = *(void **)head;
            if (count-- & 1) {
                kfree(ptr);
            } else {
                *(void **)ptr = deferred;
                deferred = ptr;
            }
        }
        while (deferred) {
            void *ptr = deferred;
            deferred = *(void **)deferred;
            kfree(ptr);
        }
    }
    return true;
}

bool verify_heap() {
    CHECK(!kmalloc(0) && !kmalloc(~0U) && !kmalloc(1U << 31));
    CHECK(!kcalloc(0, 16) && !kcalloc(16, 0) && !kcalloc(~0U, 16));
    CHECK(!get_free_pages(~0U));
    kfree(nullptr);
    u8 *page = (u8 *)unified_allocator.alloc_pages(0, ALLOC_FLAG_ZERO);
    CHECK(page);
    for (u32 i = 0; i < PAGE_SIZE; ++i) CHECK(!page[i]);
    fill(page, PAGE_SIZE, 41);
    free_pages(page, 0);
    const system_mode modes[] = {SYS_MODE_EMBEDDED, SYS_MODE_DESKTOP,
                                 SYS_MODE_SERVER, SYS_MODE_REALTIME};
    for (system_mode mode : modes) {
        unified_allocator.set_system_mode(mode);
        CHECK(reallocation());
        CHECK(mixed_allocations());
    }
    CHECK(cache_reuse());
    CHECK(tracking_and_modes());
    CHECK(exhaustion());

    u8 *temporary = (u8 *)kmalloc_temp(63);
    u8 *scoped = (u8 *)kmalloc_scoped(127);
    CHECK(temporary && scoped);
    fill(temporary, 63, 29);
    fill(scoped, 127, 31);
    u8 *grown = (u8 *)krealloc(temporary, 255);
    CHECK(grown && matches(grown, 63, 29) && matches(scoped, 127, 31));
    kfree(grown);
    kfree(scoped);
    kstack_reset();
    serial::write("[HEAP] allocation, reuse, realloc and exhaustion verified\n");
    return true;
}
