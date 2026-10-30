#include <os.h>
#include <memory_file.h>
#include <device.h>
#include <arch/x86/keyboard.h>

MemoryFile::MemoryFile(const char *name, const void *bytes, u32 count) : File(name, TYPE_FILE), data((const u8 *)bytes) {
    setSize(count);
}

u32 MemoryFile::open(u32 flags) { return flags ? (u32)-30 : 0; }
u32 MemoryFile::close() { return 0; }

u32 MemoryFile::read(u32 offset, u8 *buffer, u32 count) {
    if (offset >= getSize()) return 0;
    if (count > getSize() - offset) count = getSize() - offset;
    for (u32 i = 0; i < count; ++i) buffer[i] = data[offset + i];
    return count;
}

class Console : public Device {
public:
    Console() : Device("tty") {}
    u32 read(u32, u8 *buffer, u32 count) override {
        u32 bytes = 0;
        while (bytes < count) {
            char value = 0;
            if (io.inb(0x3fd) & 1) value = io.inb(0x3f8);
            else if (keyboard_available()) value = keyboard_getchar();
            else break;
            buffer[bytes++] = value;
        }
        return bytes ? bytes : (u32)-11;
    }
    u32 write(u32, u8 *buffer, u32 count) override {
        for (u32 i = 0; i < count; ++i) io.putc(buffer[i]);
        return count;
    }
};

void init_console() { new Console(); }
