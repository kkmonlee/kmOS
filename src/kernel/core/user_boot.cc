#include <os.h>
#include <boot.h>
#include <elf_loader.h>
#include <architecture.h>
#include <arch/x86/serial.h>

void run_user_modules(const multiboot_info *boot) {
    if (!(boot->flags & 4) || !boot->cmdline || boot->cmdline > USER_OFFSET - 4096 ||
        !boot_has_argument((const char *)boot->cmdline, "user")) return;
    u32 limit = vmm.frame_count * PAGESIZE;
    if (limit > USER_OFFSET) limit = USER_OFFSET;
    if (!(boot->flags & 8) || !boot->mods_count || boot->mods_count > 32 ||
        !boot->mods_addr || boot->mods_addr >= limit ||
        boot->mods_count > (limit - boot->mods_addr) / sizeof(multiboot_module)) {
        serial::write("[USER] No valid boot modules\n");
        return;
    }
    const multiboot_module *modules = (const multiboot_module *)boot->mods_addr;
    for (u32 i = 0; i < boot->mods_count; ++i) {
        const multiboot_module &module = modules[i];
        if (!module.start || module.start >= module.end || module.end > limit) {
            serial::write("[USER] Rejected boot module\n");
            continue;
        }
        u32 frames = vmm.frames_used;
        Process *process = new Process(const_cast<char *>("init"));
        process->setPParent(arch.pcurrent);
        const char *args[] = {"init"};
        if (process->create((const void *)module.start, module.end - module.start, 1, args) != RETURN_OK) {
            serial::write("[USER] Rejected ELF\n");
            delete process;
            continue;
        }
        serial::write("[USER] Started init\n");
        while (process->getState() != ZOMBIE) asm volatile("hlt" ::: "memory");
        serial::write("[USER] Exit status=");
        serial::hex(process->getExitStatus());
        serial::write("\n");
        arch.destroy_process(process);
        serial::write(vmm.frames_used == frames ? "[USER] Reaped init\n" : "[USER] Frame leak\n");
    }
}
