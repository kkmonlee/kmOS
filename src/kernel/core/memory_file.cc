#include <os.h>
#include <memory_file.h>
#include <device.h>
#include <architecture.h>
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

    void set_owner(Process *process) {
        IRQGuard guard;
        owner_ = process;
        foreground_ = 0;
        first_ = count_ = 0;
    }

    int set_foreground(u32 operation, u32 group) {
        if (operation != 1) return -25;
        if (!owner_ || arch.pcurrent != owner_) return -1;
        if (group > 0x7fffffffU) return -22;
        foreground_ = group;
        if (count_) Process::wakeFile(this, input_group());
        return 0;
    }

    void poll() {
        if (!owner_) return;
        for (u32 i = 0; i < sizeof(input_); ++i) {
            u8 value;
            if (io.inb(0x3fd) & 1) value = io.inb(0x3f8);
            else if (keyboard_available()) value = keyboard_getchar();
            else break;
            if ((value == 3 || value == 26) && foreground_) {
                first_ = count_ = 0;
                signal_processes(-static_cast<s32>(foreground_), value == 3 ? SIGINT : SIGTSTP);
            } else if (count_ < sizeof(input_)) {
                input_[(first_ + count_) % sizeof(input_)] = value;
                ++count_;
            }
        }
        if (count_) Process::wakeFile(this, input_group());
    }

    bool waitable() const override { return true; }

    u32 read(u32, u8 *buffer, u32 count) override {
        Process *process = arch.pcurrent;
        if (owner_ && process->getGroup() != input_group()) {
            if (!process->receivesSignal(SIGTTIN)) return (u32)-5;
            signal_processes(-static_cast<s32>(process->getGroup()), SIGTTIN);
            return process->getState() == STOPPED ? (u32)-11 : (u32)-4;
        }
        if (!count_) return (u32)-11;
        if (count > count_) count = count_;
        u32 copied = 0;
        while (copied < count) {
            u8 value = input_[first_];
            if (value == 4 && foreground_ && copied) break;
            first_ = (first_ + 1) % sizeof(input_);
            --count_;
            if (value == 4 && foreground_) break;
            buffer[copied++] = value;
        }
        return copied;
    }

    u32 write(u32, u8 *buffer, u32 count) override {
        for (u32 i = 0; i < count; ++i) io.putc(buffer[i]);
        return count;
    }

private:
    u32 input_group() const { return foreground_ ? foreground_ : owner_->getGroup(); }

    Process *owner_ = nullptr;
    u32 foreground_ = 0;
    u32 first_ = 0;
    u32 count_ = 0;
    u8 input_[512];
};

static Console *console;

void init_console() { console = new Console(); }
void set_console_owner(Process *process) { if (console) console->set_owner(process); }
void poll_console() { if (console) console->poll(); }
int set_console_foreground(File *file, u32 operation, u32 group) {
    return file && file == console ? console->set_foreground(operation, group) : -25;
}
