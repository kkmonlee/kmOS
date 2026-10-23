#ifndef SERIAL_H
#define SERIAL_H

#include <runtime/types.h>

namespace serial {
inline void out(u16 port, u8 value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

inline void init() {
    out(0x3F9, 0);
    out(0x3FB, 0x80);
    out(0x3F8, 3);
    out(0x3F9, 0);
    out(0x3FB, 3);
    out(0x3FA, 0xC7);
    out(0x3FC, 0x0B);
}

inline void write(const char *text) {
    while (*text) {
        u8 status;
        do {
            asm volatile("inb %1, %0" : "=a"(status) : "Nd"((u16)0x3FD));
        } while (!(status & 0x20));
        out(0x3F8, *text++);
    }
}

inline void hex(u32 value) {
    char text[9];
    for (u32 i = 0; i < 8; ++i) text[i] = "0123456789abcdef"[(value >> (28 - i * 4)) & 15];
    text[8] = 0;
    write(text);
}
}

#endif
