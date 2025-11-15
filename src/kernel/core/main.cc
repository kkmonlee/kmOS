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
    
    Process *kernel_process = new Process(const_cast<char*>("kernel_main"));
    kernel_process->setPParent(nullptr);
    arch.addProcess(kernel_process);

    serial_print("KMAIN: arch.init() completed successfully!\n");
    
    // Initialize core subsystems that shell relies on
    serial_print("KMAIN: Initializing filesystem\n");
    fsm.init();
    serial_print("KMAIN: Filesystem initialized\n");

    fsm.register_driver(&ext2_driver);

    serial_print("KMAIN: Initializing ATA devices\n");
    
    // Note: ATA init can hang in QEMU without a disk, so we skip for now
    // Uncomment when testing with actual disk images
    // ata_init();
    //
    // ATADevice* disk = ata_primary_master();
    // if (disk && disk->identify().present) {
    //     serial_print("KMAIN: Primary ATA device detected\n");
    //     if (!fsm.path("/mnt/disk")) {
    //         File* mnt = fsm.path("/mnt");
    //         if (mnt) {
    //             mnt->createChild("disk", TYPE_DIRECTORY);
    //         }
    //     }
    //     if (fsm.mount(disk, "/mnt/disk", "ext2")) {
    //         serial_print("KMAIN: ext2 filesystem mounted at /mnt/disk\n");
    //     } else {
    //         serial_print("KMAIN: Failed to mount ext2 filesystem\n");
    //     }
    // } else {
    //     serial_print("KMAIN: No ATA disk detected\n");
    // }
    
    serial_print("KMAIN: ATA initialization skipped (no disk in test environment)\n");
    
    // If we get here, write success message
    const char *msg3 = "ARCH INIT SUCCESS!";
    for (int i = 0; msg3[i] != '\0'; i++) {
        video_memory[160 + i] = (0x0A << 8) | msg3[i]; // Green text
    }
    
    serial_print("KMAIN: Initializing keyboard driver\n");
    
    // Initialize keyboard for shell input
    keyboard.init();
    
    // Set up IDT and PIC, then enable IRQs so keyboard uses IRQ1
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
    
    // If shell exits, keep kernel alive
    while(1) {
        asm volatile("hlt");
    }
}