#include <os.h>
#include <arch/x86/architecture.h>
#include <arch/x86/keyboard.h>
#include <arch/x86/pit.h>
#include <arch/x86/serial.h>
#include <core/system.h>
#include <core/filesystem.h>
#include <core/syscalls.h>
#include <core/shell.h>
#include <core/ext2.h>
#include <core/process.h>
#include <core/boot.h>

Architecture arch;
IO io;
System sys;
Filesystem fsm;
Syscalls syscall;

void run_boot_test(const multiboot_info *boot);
void run_filesystem_test(const multiboot_info *boot);
void run_user_modules(const multiboot_info *boot);

extern "C" void kmain(u32 magic, const multiboot_info *boot) {
    serial::init();
    if (magic != MULTIBOOT_MAGIC || !boot) {
        serial::write("[PANIC] Invalid Multiboot information\n");
        for (;;) asm volatile("cli; hlt");
    }
    init_gdt();
    init_idt();
    init_pic();
    if (!verify_exception_frame()) {
        serial::write("[PANIC] Exception frame verification failed\n");
        for (;;) asm volatile("cli; hlt");
    }
    serial::write("[CPU] Exception frames verified\n");
    arch.init(boot);
    run_boot_test(boot);

    fsm.init();
    fsm.register_driver(&ext2_driver);
    Process *kernel_process = new Process(const_cast<char *>("kernel_main"));
    kernel_process->setPParent(nullptr);
    kernel_process->getPInfo()->pd = kernel_directory;
    arch.addProcess(kernel_process);
    run_filesystem_test(boot);
    keyboard.init();
    pit.init(100);
    arch.configure_scheduler(pit.get_frequency());
    shell.init();
    syscall.init();
    arch.enable_interrupt();
    run_user_modules(boot);
    serial::write("[BOOT] Ready\n");
    shell.run();
    for (;;) asm volatile("hlt");
}
