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

// Serial port functions for debugging
static void serial_outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static void init_serial() {
    serial_outb(0x3F8 + 1, 0x00);    // Disable interrupts
    serial_outb(0x3F8 + 3, 0x80);    // Enable DLAB
    serial_outb(0x3F8 + 0, 0x03);    // Set divisor to 3 (38400 baud)
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x03);    // 8 bits, no parity, one stop bit
    serial_outb(0x3F8 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    serial_outb(0x3F8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static void serial_print(const char* str) {
    while (*str) {
        serial_outb(0x3F8, *str);
        str++;
    }
}

extern "C" void kmain()
{
    // Initialize serial port first
    init_serial();
    serial_print("KMAIN: Serial port initialized\n");
    
    // Direct VGA memory write for immediate feedback
    volatile unsigned short *video_memory = (volatile unsigned short*)0xB8000;
    const char *message = "KMAIN REACHED - SERIAL WORKS";
    
    serial_print("KMAIN: About to clear VGA screen\n");
    
    // Clear screen first
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = 0x0720; // Space character with light gray on black
    }
    
    // Write our test message
    for (int i = 0; message[i] != '\0'; i++) {
        video_memory[i] = (0x0F << 8) | message[i]; // White text on black background
    }
    
    serial_print("KMAIN: VGA message written\n");
    
    // Write a second line
    const char *msg2 = "TRYING ARCH INIT...";
    for (int i = 0; msg2[i] != '\0'; i++) {
        video_memory[80 + i] = (0x0E << 8) | msg2[i]; // Yellow text
    }
    
    serial_print("KMAIN: About to call arch.init()\n");
    
    // Try basic architecture initialization - this might hang
    arch.init();
    
    serial_print("KMAIN: arch.init() completed successfully!\n");
    
    // If we get here, write success message
    const char *msg3 = "ARCH INIT SUCCESS!";
    for (int i = 0; msg3[i] != '\0'; i++) {
        video_memory[160 + i] = (0x0A << 8) | msg3[i]; // Green text
    }
    
    serial_print("KMAIN: Entering main loop\n");
    
    // Keep kernel alive
    while(1) {
        asm volatile("hlt");
    }
}