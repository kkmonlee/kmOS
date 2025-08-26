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
    // Direct VGA memory write for immediate feedback
    volatile unsigned short *video_memory = (volatile unsigned short*)0xB8000;
    const char *message = "KMAIN REACHED - BASIC TEST";
    
    // Clear screen first
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = 0x0720; // Space character with light gray on black
    }
    
    // Write our test message
    for (int i = 0; message[i] != '\0'; i++) {
        video_memory[i] = (0x0F << 8) | message[i]; // White text on black background
    }
    
    // Write a second line
    const char *msg2 = "TRYING ARCH INIT...";
    for (int i = 0; msg2[i] != '\0'; i++) {
        video_memory[80 + i] = (0x0E << 8) | msg2[i]; // Yellow text
    }
    
    // Try basic architecture initialization
    arch.init(); // This might be where it hangs
    
    // If we get here, write success message
    const char *msg3 = "ARCH INIT SUCCESS!";
    for (int i = 0; msg3[i] != '\0'; i++) {
        video_memory[160 + i] = (0x0A << 8) | msg3[i]; // Green text
    }
    
    // Keep kernel alive
    while(1) {
        asm volatile("hlt");
    }
}