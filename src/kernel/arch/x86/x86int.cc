#include <os.h>
#include <vmm.h>

extern "C" {
    void interrupt_handler(int interrupt_number) {
        io.print("[INT] Unhandled interrupt %d\n", interrupt_number);
    }
    
    void isr_GP_exc() {
        io.print("[PANIC] General Protection Exception\n");
        while(1);
    }
    
    void isr_PF_exc(regs_t *regs) {
        u32 fault_addr;
        
        asm volatile("mov %%cr2, %0" : "=r" (fault_addr));
        
        int result = vmm.handle_page_fault(fault_addr, regs->err_code);
        if (result != 0) {
            io.print("[PANIC] Unhandled page fault at %x, error %x\n", fault_addr, regs->err_code);
            while(1);
        }
    }
    
    void isr_default_int() {
    }
    
    void isr_schedule_int() {
    }
}