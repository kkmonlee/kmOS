#include <os.h>
#include <arch/x86/architecture.h>
#include <arch/x86/io.h>
#include <core/system.h>
#include <core/filesystem.h>
#include <core/syscalls.h>

Architecture arch;
IO io;
System sys;
Filesystem fsm;
Syscalls syscall;

extern "C" void kmain()
{
    io.clear();
    io.setColor(IO::White, IO::Black);
    io.print("kmOS Kernel v0.1 - Starting...\n");
    
    io.print("[INIT] Initializing architecture...\n");
    arch.init();
    
    io.print("[INIT] Initializing system...\n");
    sys.init();
    
    io.print("[INIT] Initializing filesystem...\n");
    fsm.init();
    
    io.print("[INIT] Initializing syscalls...\n");
    syscall.init();
    
    io.print("[INIT] Kernel initialization complete!\n");
    io.print("[KERN] kmOS ready - entering idle loop\n");
    
    arch.enable_interrupt();
    
    while(1) {
        for(volatile int i = 0; i < 1000000; i++);
    }
}