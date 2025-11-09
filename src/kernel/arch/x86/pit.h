#ifndef PIT_H
#define PIT_H

#include <runtime/types.h>

#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

#define PIT_BASE_FREQ   1193182
#define PIT_DEFAULT_FREQ 100

class PIT {
public:
    void init(u32 frequency = PIT_DEFAULT_FREQ);
    void tick();
    u64 get_ticks();
    u64 get_uptime_ms();
    u32 get_frequency();
    void sleep(u32 ms);

private:
    u64 system_ticks;
    u32 timer_frequency;
};

extern PIT pit;

extern "C" {
    void pit_tick();
    u64 get_system_ticks();
    u64 get_uptime_ms();
}

#endif
