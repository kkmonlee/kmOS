#ifndef X86_STORAGE_TIMEOUT_H
#define X86_STORAGE_TIMEOUT_H

#include <arch/x86/io.h>
#include <arch/x86/pit.h>

extern IO io;

class StorageTimeout {
public:
    StorageTimeout() : speaker_(io.inb(speaker_port)), remaining_(5 * 20) { arm(); }
    ~StorageTimeout() { io.outb(speaker_port, speaker_); }

    bool expired() {
        if (!(io.inb(speaker_port) & channel2_output)) return false;
        if (!--remaining_) return true;
        arm();
        return false;
    }

private:
    static constexpr u16 speaker_port = 0x61;
    static constexpr u8 channel2_output = 0x20;
    static constexpr u16 ticks = PIT_BASE_FREQ / 20;

    void arm() {
        u8 speaker = speaker_ & ~3;
        io.outb(speaker_port, speaker);
        io.outb(PIT_COMMAND, 0xb0);
        io.outb(PIT_CHANNEL2, (u8)ticks);
        io.outb(PIT_CHANNEL2, (u8)(ticks >> 8));
        io.outb(speaker_port, speaker | 1);
    }

    u8 speaker_;
    u32 remaining_;
};

#endif
