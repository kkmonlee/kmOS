#include <os.h>
#include <pit.h>
#include <architecture.h>
#include <core/process.h>

static volatile u32 timer_interrupt_count = 0;

extern "C" {
    void isr_timer_int() {
        timer_interrupt_count++;
        pit.tick();
        
        Process *current = arch.pcurrent;
        if (current != NULL) {
            Process *next = current->schedule();
            
            if (next != current && next != NULL) {
                arch.pcurrent = next;
            }
        }
    }
    
    u32 get_timer_interrupt_count() {
        return timer_interrupt_count;
    }
}
