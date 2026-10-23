#ifndef PHYSICAL_MEMORY_H
#define PHYSICAL_MEMORY_H

#include <runtime/types.h>

class PhysicalMemory {
public:
    static constexpr u32 FRAME_BYTES = 4096;
    static constexpr u32 LIMIT = 0x40000000;
    static constexpr u32 MAX_FRAMES = LIMIT / FRAME_BYTES;

    void reset() {
        count_ = 0;
        free_ = 0;
        next_ = 1;
        for (u32 i = 0; i < MAX_FRAMES / 32; ++i) {
            used_[i] = ~0U;
            usable_[i] = 0;
        }
    }

    void add_usable(u64 start, u64 length) {
        if (start >= LIMIT || !length) return;
        u64 end = length > LIMIT - start ? LIMIT : start + length;
        u32 first = (u32)((start + FRAME_BYTES - 1) / FRAME_BYTES);
        u32 last = (u32)(end / FRAME_BYTES);
        if (last > count_) count_ = last;
        for (u32 i = first; i < last; ++i) {
            u32 mask = 1U << (i % 32);
            if (!(usable_[i / 32] & mask)) {
                usable_[i / 32] |= mask;
                used_[i / 32] &= ~mask;
                ++free_;
                if (i && i < next_) next_ = i;
            }
        }
    }

    void reserve(u64 start, u64 length) {
        if (start >= LIMIT || !length) return;
        u64 end = length > LIMIT - start ? LIMIT : start + length;
        u32 last = (u32)((end + FRAME_BYTES - 1) / FRAME_BYTES);
        for (u32 i = (u32)(start / FRAME_BYTES); i < last; ++i) {
            u32 mask = 1U << (i % 32);
            if (!(used_[i / 32] & mask)) --free_;
            used_[i / 32] |= mask;
            usable_[i / 32] &= ~mask;
        }
    }

    u32 allocate(u32 pages = 1) {
        if (!pages || pages > free_) return 0;
        u32 run = 0;
        u32 first_free = count_;
        for (u32 i = next_; i < count_; ++i) {
            if (used_[i / 32] & (1U << (i % 32))) {
                run = 0;
            } else {
                if (first_free == count_) first_free = i;
                if (++run != pages) continue;
                u32 first = i + 1 - pages;
                for (u32 j = first; j <= i; ++j) used_[j / 32] |= 1U << (j % 32);
                free_ -= pages;
                next_ = first_free == first ? i + 1 : first_free;
                return first * FRAME_BYTES;
            }
        }
        return 0;
    }

    void release(u32 address) {
        if (!is_usable(address)) return;
        u32 index = address / FRAME_BYTES;
        u32 mask = 1U << (index % 32);
        if (used_[index / 32] & mask) {
            used_[index / 32] &= ~mask;
            ++free_;
            if (index < next_) next_ = index;
        }
    }

    bool is_usable(u32 address) const {
        u32 index = address / FRAME_BYTES;
        return address % FRAME_BYTES == 0 && index < count_ &&
            (usable_[index / 32] & (1U << (index % 32)));
    }

    u32 take_heap(u32 pages) {
        u32 start = allocate(pages);
        if (start) reserve(start, (u64)pages * FRAME_BYTES);
        return start;
    }

    u32 frame_count() const { return count_; }
    u32 free_pages() const { return free_; }

private:
    u32 used_[MAX_FRAMES / 32];
    u32 usable_[MAX_FRAMES / 32];
    u32 count_;
    u32 free_;
    u32 next_;
};

#endif
