#include <os.h>
#include <arch/x86/architecture.h>
#include <arch/x86/io.h>
#include <arch/x86/keyboard.h>
#include <arch/x86/pit.h>
#include <arch/x86/ata.h>
#include <core/system.h>
#include <core/filesystem.h>
#include <core/syscalls.h>
#include <core/shell.h>
#include <core/ext2.h>
#include <core/process.h>

Architecture arch;
IO io;
System sys;
Filesystem fsm;
Syscalls syscall;

static void serial_outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static void init_serial() {
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x80);
    serial_outb(0x3F8 + 0, 0x03);
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x03);
    serial_outb(0x3F8 + 2, 0xC7);
    serial_outb(0x3F8 + 4, 0x0B);
}

static void serial_print(const char* str) {
    while (*str) {
        serial_outb(0x3F8, *str);
        str++;
    }
}

extern "C" void kmain()
{
    init_serial();
    serial_print("KMAIN: Serial port initialized\n");
    
    volatile unsigned short *video_memory = (volatile unsigned short*)0xB8000;
    const char *message = "KMAIN REACHED - SERIAL WORKS";
    
    serial_print("KMAIN: About to clear VGA screen\n");
    
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = 0x0720;
    }
    
    for (int i = 0; message[i] != '\0'; i++) {
        video_memory[i] = (0x0F << 8) | message[i];
    }
    
    serial_print("KMAIN: VGA message written\n");
    
    const char *msg2 = "TRYING ARCH INIT...";
    for (int i = 0; msg2[i] != '\0'; i++) {
        video_memory[80 + i] = (0x0E << 8) | msg2[i];
    }
    
    serial_print("KMAIN: About to call arch.init()\n");
    
    arch.init();
    
    Process *kernel_process = new Process(const_cast<char*>("kernel_main"));
    kernel_process->setPParent(nullptr);
    arch.addProcess(kernel_process);

    serial_print("KMAIN: arch.init() completed successfully!\n");
    
    serial_print("KMAIN: Initializing filesystem\n");
    fsm.init();
    serial_print("KMAIN: Filesystem initialized\n");

    fsm.register_driver(&ext2_driver);

    serial_print("KMAIN: Initializing ATA devices\n");
    
    serial_print("KMAIN: ATA initialization skipped (no disk in test environment)\n");
    
    const char *msg3 = "ARCH INIT SUCCESS!";
    for (int i = 0; msg3[i] != '\0'; i++) {
        video_memory[160 + i] = (0x0A << 8) | msg3[i];
    }
    
    serial_print("KMAIN: Initializing keyboard driver\n");
    
    keyboard.init();
    
    serial_print("KMAIN: Initializing IDT and PIC\n");
    init_idt();
    init_pic();
    
    serial_print("KMAIN: Initializing PIT timer\n");
    pit.init(100);
    arch.configure_scheduler(pit.get_frequency());
    serial_print("[TIMER] PIT initialized at 100 Hz\n");
    serial_print("[SCHEDULER] Configured with 20ms quantum\n");
    
    serial_print("KMAIN: Enabling interrupts\n");
    arch.enable_interrupt();
    
    serial_print("KMAIN: Initializing shell\n");
    
    shell.init();
    
    serial_print("KMAIN: Starting interactive shell\n");
    
    shell.run();
    
    serial_print("KMAIN: Shell exited, entering halt loop\n");
    
    while(1) {
        asm volatile("hlt");
    }
}