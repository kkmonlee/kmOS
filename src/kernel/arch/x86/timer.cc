#include <os.h>
#include <pit.h>
#include <architecture.h>
#include <core/process.h>

static volatile u32 timer_interrupt_count = 0;

extern "C" {
    void isr_timer_int(InterruptFrame *frame) {
        timer_interrupt_count++;
        pit.tick();
        arch.handle_timer_interrupt(frame);
    }
    
    u32 get_timer_interrupt_count() {
        return timer_interrupt_count;
    }
}
