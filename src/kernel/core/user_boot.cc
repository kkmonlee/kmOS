#include <os.h>
#include <boot.h>
#include <elf_loader.h>
#include <memory_file.h>
#include <filesystem.h>
#include <architecture.h>
#include <arch/x86/serial.h>

extern "C" int strcmp(const char *, const char *);

static bool module_name(const multiboot_module &module, u32 limit, char *name) {
    if (!module.string || module.string >= limit) return false;
    const char *text = (const char *)module.string;
    u32 length = 0;
    for (u32 i = 0; i < 4096 && module.string + i < limit; ++i) {
        char value = text[i];
        if (!value || value == ' ') {
            if (length > 4 && !strcmp(name + length - 4, ".elf")) length -= 4;
            name[length] = 0;
            return length && strcmp(name, ".") && strcmp(name, "..");
        }
        if (value == '/') length = 0;
        else {
            if (length == File::kMaxNameLength) return false;
            name[length++] = value;
        }
        name[length] = 0;
    }
    return false;
}

static void start_user_program(const multiboot_module &module) {
    u32 frames = vmm.frames_used;
    u32 processes = arch.process_count();
    Process *process = new Process(const_cast<char *>("init"));
    process->setPParent(arch.pcurrent);
    const char *args[] = {"init"};
    if (process->create((const void *)module.start, module.end - module.start, 1, args) != RETURN_OK) {
        serial::write("[USER] Rejected ELF\n");
        delete process;
        return;
    }
    serial::write("[USER] Started init\n");
    while (process->getState() != ZOMBIE) asm volatile("hlt" ::: "memory");
    serial::write("[USER] Exit status=");
    serial::hex(process->getExitStatus());
    serial::write("\n");
    arch.destroy_process(process);
    while (arch.process_count() > processes) {
        arch.destroy_all_zombie();
        if (arch.process_count() > processes) asm volatile("hlt" ::: "memory");
    }
    serial::write(vmm.frames_used == frames ? "[USER] Reaped init\n" : "[USER] Frame leak\n");
}

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
    File *bin = fsm.path("/bin");
    if (!bin) bin = fsm.getRoot()->createChild("bin", TYPE_DIRECTORY);
    File *etc = fsm.path("/etc");
    if (!etc) etc = fsm.getRoot()->createChild("etc", TYPE_DIRECTORY);
    static const char motd[] = "kmOS userland\n";
    if (etc && !etc->find("motd")) etc->addChild(new MemoryFile("motd", motd, sizeof(motd) - 1));
    const multiboot_module *modules = (const multiboot_module *)boot->mods_addr;
    bool valid[32] = {};
    int init = -1;
    for (u32 i = 0; i < boot->mods_count; ++i) {
        const multiboot_module &module = modules[i];
        if (!module.start || module.start >= module.end || module.end > limit) {
            serial::write("[USER] Rejected boot module\n");
            continue;
        }
        valid[i] = true;
        char name[File::kMaxNameLength + 1];
        if (bin && module_name(module, limit, name) && !bin->find(name)) {
            MemoryFile *file = new MemoryFile(name, (const void *)module.start, module.end - module.start);
            if (bin->addChild(file) != RETURN_OK) delete file;
            else if (!strcmp(name, "init")) init = i;
        }
    }
    if (init >= 0) start_user_program(modules[init]);
    else for (u32 i = 0; i < boot->mods_count; ++i) if (valid[i]) start_user_program(modules[i]);
}
