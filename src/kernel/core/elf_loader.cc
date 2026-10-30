#include <os.h>
#include <elf_loader.h>
#include <filesystem.h>
#include <architecture.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    void *memset(void *dest, int c, int n);
}

static const u32 MAX_IMAGE_SIZE = 16 * 1024 * 1024;
static const u32 MAX_ARGUMENTS = 32;
static const u32 PT_LOAD = 1, PT_DYNAMIC = 2, PT_INTERP = 3, PT_TLS = 7;
static const u32 PF_X = 1, PF_W = 2, PF_R = 4;

bool is_elf(const void *file, u32 size) {
    if (!file || size < sizeof(Elf32_Ehdr) || size > MAX_IMAGE_SIZE) return false;
    const Elf32_Ehdr *header = (const Elf32_Ehdr *)file;
    const u8 *id = header->e_ident;
    return id[0] == 0x7f && id[1] == 'E' && id[2] == 'L' && id[3] == 'F' &&
        id[4] == 1 && id[5] == 1 && id[6] == 1 && header->e_type == 2 &&
        header->e_machine == 3 && header->e_version == 1 &&
        header->e_ehsize == sizeof(Elf32_Ehdr) && header->e_phentsize == sizeof(Elf32_Phdr) &&
        header->e_phnum && header->e_phnum <= 128 && header->e_phoff >= sizeof(Elf32_Ehdr) &&
        header->e_phoff <= size && header->e_phnum <= (size - header->e_phoff) / sizeof(Elf32_Phdr);
}

static bool copy_to_user(page_directory *pd, u32 address, const void *source, u32 size) {
    if (!vmm.user_range(pd, address, size, true)) return false;
    const u8 *bytes = (const u8 *)source;
    while (size) {
        u32 chunk = PAGESIZE - (address & (PAGESIZE - 1));
        if (chunk > size) chunk = size;
        memcpy((void *)vmm.get_physical_addr(pd, address), bytes, chunk);
        address += chunk;
        bytes += chunk;
        size -= chunk;
    }
    return true;
}

static bool map_zeroed(page_directory *pd, u32 begin, u32 end, u32 flags) {
    for (u32 address = begin; address < end; address += PAGESIZE) {
        u32 frame = vmm.alloc_frame();
        if (!frame) return false;
        memset((void *)frame, 0, PAGESIZE);
        if (vmm.map_page(pd, address, frame, PG_PRESENT | PG_USER | flags)) {
            vmm.free_frame(frame);
            return false;
        }
    }
    return true;
}

u32 load_elf(const void *file, u32 size, process_st *proc, int argc, const char *const *argv) {
    if (!proc || proc->pd || !is_elf(file, size) || argc < 0 || argc > (int)MAX_ARGUMENTS ||
        (argc && !argv)) return 0;
    u32 lengths[MAX_ARGUMENTS];
    u32 argument_bytes = 0;
    for (int i = 0; i < argc; ++i) {
        if (!argv[i]) return 0;
        u32 length = 0;
        while (length < 256 && argv[i][length]) ++length;
        if (length == 256 || argument_bytes + length + 1 > PAGESIZE) return 0;
        lengths[i] = length + 1;
        argument_bytes += length + 1;
    }
    const u8 *bytes = (const u8 *)file;
    const Elf32_Ehdr *header = (const Elf32_Ehdr *)file;
    const Elf32_Phdr *segments = (const Elf32_Phdr *)(bytes + header->e_phoff);
    bool executable_entry = false;
    u32 previous_end = USER_OFFSET;
    u32 mapped_bytes = 0;
    for (u32 i = 0; i < header->e_phnum; ++i) {
        const Elf32_Phdr &segment = segments[i];
        if (segment.p_type == PT_DYNAMIC || segment.p_type == PT_INTERP || segment.p_type == PT_TLS) return 0;
        if (segment.p_type != PT_LOAD) continue;
        if (segment.p_filesz > segment.p_memsz || segment.p_offset > size ||
            segment.p_filesz > size - segment.p_offset || segment.p_vaddr < USER_OFFSET ||
            segment.p_vaddr >= USER_IMAGE_LIMIT || segment.p_memsz > USER_IMAGE_LIMIT - segment.p_vaddr ||
            !(segment.p_flags & PF_R) || (segment.p_flags & ~(PF_R | PF_W | PF_X)) ||
            (segment.p_flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
            (segment.p_offset & (PAGESIZE - 1)) != (segment.p_vaddr & (PAGESIZE - 1))) return 0;
        if (segment.p_align > 1 && ((segment.p_align & (segment.p_align - 1)) ||
            (segment.p_offset & (segment.p_align - 1)) != (segment.p_vaddr & (segment.p_align - 1)))) return 0;
        if (!segment.p_memsz) continue;
        u32 begin = segment.p_vaddr & ~(PAGESIZE - 1);
        u32 end = (segment.p_vaddr + segment.p_memsz + PAGESIZE - 1) & ~(PAGESIZE - 1);
        if (begin < previous_end || end - begin > MAX_IMAGE_SIZE - mapped_bytes) return 0;
        mapped_bytes += end - begin;
        previous_end = end;
        if ((segment.p_flags & PF_X) && header->e_entry >= segment.p_vaddr &&
            header->e_entry - segment.p_vaddr < segment.p_filesz) executable_entry = true;
    }
    if (!executable_entry) return 0;

    page_directory *pd = vmm.create_page_directory();
    if (!pd) return 0;
    process_st loaded = {};
    loaded.pd = pd;
    for (u32 i = 0; i < header->e_phnum; ++i) {
        const Elf32_Phdr &segment = segments[i];
        if (segment.p_type != PT_LOAD || !segment.p_memsz) continue;
        u32 begin = segment.p_vaddr & ~(PAGESIZE - 1);
        u32 end = (segment.p_vaddr + segment.p_memsz + PAGESIZE - 1) & ~(PAGESIZE - 1);
        if (!map_zeroed(pd, begin, end, (segment.p_flags & PF_W) ? PG_WRITE : 0)) {
            vmm.destroy_page_directory(pd);
            return 0;
        }
        for (u32 offset = 0; offset < segment.p_filesz;) {
            u32 address = segment.p_vaddr + offset;
            u32 chunk = PAGESIZE - (address & (PAGESIZE - 1));
            if (chunk > segment.p_filesz - offset) chunk = segment.p_filesz - offset;
            memcpy((void *)vmm.get_physical_addr(pd, address), bytes + segment.p_offset + offset, chunk);
            offset += chunk;
        }
        if (segment.p_flags & PF_X) {
            if (!loaded.b_exec) loaded.b_exec = (char *)segment.p_vaddr;
            loaded.e_exec = (char *)(segment.p_vaddr + segment.p_memsz);
        }
        if (segment.p_flags & PF_W) {
            if (!loaded.b_bss) loaded.b_bss = (char *)segment.p_vaddr;
            loaded.e_bss = (char *)(segment.p_vaddr + segment.p_memsz);
        }
    }
    if (!map_zeroed(pd, USER_STACK_BASE, USER_STACK, PG_WRITE)) {
        vmm.destroy_page_directory(pd);
        return 0;
    }
    u32 stack = USER_STACK;
    u32 words[MAX_ARGUMENTS + 3] = {};
    words[0] = argc;
    for (int i = argc - 1; i >= 0; --i) {
        stack -= lengths[i];
        words[i + 1] = stack;
        if (!copy_to_user(pd, stack, argv[i], lengths[i])) {
            vmm.destroy_page_directory(pd);
            return 0;
        }
    }
    u32 word_bytes = (argc + 3) * sizeof(u32);
    stack = (stack - word_bytes) & ~15U;
    if (!copy_to_user(pd, stack, words, word_bytes)) {
        vmm.destroy_page_directory(pd);
        return 0;
    }
    loaded.regs.eip = header->e_entry;
    loaded.regs.esp = stack;
    loaded.b_heap = loaded.e_heap = (char *)previous_end;
    proc->pd = loaded.pd;
    proc->regs.eip = loaded.regs.eip;
    proc->regs.esp = loaded.regs.esp;
    proc->b_exec = loaded.b_exec;
    proc->e_exec = loaded.e_exec;
    proc->b_bss = loaded.b_bss;
    proc->e_bss = loaded.e_bss;
    proc->b_heap = loaded.b_heap;
    proc->e_heap = loaded.e_heap;
    return header->e_entry;
}

int execv_module(const void *file, u32 size, int argc, const char *const *argv) {
    char *name = const_cast<char *>((argc > 0 && argv && argv[0]) ? argv[0] : "init");
    Process *proc = new Process(name);
    proc->setPParent(arch.pcurrent);
    if (proc->create(file, size, argc, argv) != RETURN_OK) {
        delete proc;
        return ERROR_PARAM;
    }
    return proc->getPid();
}

int execv(char *file, int argc, char **argv) {
    File *fp = fsm.path(file);
    if (!fp || !fp->getSize() || fp->getSize() > MAX_IMAGE_SIZE) return ERROR_PARAM;
    u32 size = fp->getSize();
    void *image = kmalloc(size);
    if (!image) return ERROR_MEMORY;
    fp->open(0);
    u32 count = fp->read(0, (u8 *)image, size);
    fp->close();
    int result = count == size ? execv_module(image, size, argc, argv) : ERROR_PARAM;
    kfree(image);
    return result;
}
