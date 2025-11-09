#include <os.h>
#include <pit.h>

extern IO io;
PIT pit;

static inline void outb(u16 port, u8 val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_print_pit(const char* str) {
    while (*str) {
        while ((inb(0x3F8 + 5) & 0x20) == 0);
        outb(0x3F8, *str);
        str++;
    }
}

void PIT::init(u32 frequency) {
    serial_print_pit("[PIT] Initializing Programmable Interval Timer\n");
    
    system_ticks = 0;
    timer_frequency = frequency;
    u32 divisor = PIT_BASE_FREQ / frequency;
    
    if (divisor > 65535) {
        divisor = 65535;
    }
    if (divisor < 1) {
        divisor = 1;
    }
    
    io.outb(PIT_COMMAND, 0x36);
    
    io.outb(PIT_CHANNEL0, (u8)(divisor & 0xFF));
    io.outb(PIT_CHANNEL0, (u8)((divisor >> 8) & 0xFF));
    
    io.print("[PIT] Initialized at %d Hz (divisor: %d)\n", frequency, divisor);
    serial_print_pit("[PIT] Timer initialized successfully\n");
}

void PIT::tick() {
    system_ticks++;
}

u64 PIT::get_ticks() {
    return system_ticks;
}

u64 PIT::get_uptime_ms() {
    return (system_ticks * 1000) / timer_frequency;
}

u32 PIT::get_frequency() {
    return timer_frequency;
}

void PIT::sleep(u32 ms) {
    u64 target = system_ticks + (ms * timer_frequency / 1000);
    while (system_ticks < target) {
        asm volatile("hlt");
    }
}

extern "C" {
    void pit_tick() {
        pit.tick();
    }
    
    u64 get_system_ticks() {
        return pit.get_ticks();
    }
    
    u64 get_uptime_ms() {
        return pit.get_uptime_ms();
    }
}
