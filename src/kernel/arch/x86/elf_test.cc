#include <os.h>
#include <elf_loader.h>
#include <serial.h>

extern "C" {
    void *memset(void *dest, int c, int n);
    int strcmp(const char *left, const char *right);
}

#define CHECK(condition) do { if (!(condition)) { \
    serial::write("[ELF] failed line="); \
    serial::hex(__LINE__); \
    serial::write("\n"); \
    return false; \
} } while (0)

static u8 image[3 * PAGESIZE];

static Elf32_Ehdr *make_image() {
    memset(image, 0, sizeof(image));
    Elf32_Ehdr *header = (Elf32_Ehdr *)image;
    header->e_ident[0] = 0x7f;
    header->e_ident[1] = 'E';
    header->e_ident[2] = 'L';
    header->e_ident[3] = 'F';
    header->e_ident[4] = header->e_ident[5] = header->e_ident[6] = 1;
    header->e_type = 2;
    header->e_machine = 3;
    header->e_version = 1;
    header->e_entry = USER_OFFSET;
    header->e_ehsize = sizeof(Elf32_Ehdr);
    header->e_phoff = sizeof(Elf32_Ehdr);
    header->e_phentsize = sizeof(Elf32_Phdr);
    header->e_phnum = 2;
    Elf32_Phdr *segment = (Elf32_Phdr *)(image + header->e_phoff);
    segment[0] = {1, PAGESIZE, USER_OFFSET, 0, 16, PAGESIZE, 5, PAGESIZE};
    segment[1] = {1, 2 * PAGESIZE, USER_OFFSET + PAGESIZE, 0, 16, 2 * PAGESIZE, 6, PAGESIZE};
    for (u32 i = 0; i < 16; ++i) {
        image[PAGESIZE + i] = (u8)(i + 1);
        image[2 * PAGESIZE + i] = (u8)(i + 33);
    }
    return header;
}

static bool rejected(u32 size = sizeof(image), int argc = 0, const char *const *argv = nullptr) {
    process_st process = {};
    process.pid = 123;
    u32 frames = vmm.frames_used;
    page_directory *active = current_directory;
    return !load_elf(image, size, &process, argc, argv) && !process.pd && process.pid == 123 &&
        !process.regs.eip && !process.regs.esp && current_directory == active && vmm.frames_used == frames;
}

bool verify_elf() {
    u32 frames = vmm.frames_used;
    page_directory *active = current_directory;
    make_image();
    process_st process = {};
    const char *args[] = {"init", "second"};
    CHECK(load_elf(image, sizeof(image), &process, 2, args) == USER_OFFSET);
    CHECK(current_directory == active && process.pd != active);
    CHECK(vmm.user_range(process.pd, USER_OFFSET, 3 * PAGESIZE));
    CHECK(!vmm.user_range(process.pd, USER_OFFSET, 1, true));
    CHECK(vmm.user_range(process.pd, USER_OFFSET + PAGESIZE, 2 * PAGESIZE, true));
    CHECK(!vmm.user_range(process.pd, USER_OFFSET - 1, 2));
    CHECK(!vmm.user_range(process.pd, USER_STACK - 1, 2));
    CHECK(!vmm.user_range(process.pd, USER_OFFSET, 0xffffffff));
    page_directory_entry &directory = process.pd->tables[VADDR_PD_OFFSET(USER_OFFSET)];
    directory.user = 0;
    CHECK(!vmm.user_range(process.pd, USER_OFFSET, 1));
    directory.user = 1;
    directory.writable = 0;
    CHECK(vmm.user_range(process.pd, USER_OFFSET + PAGESIZE, 1));
    CHECK(!vmm.user_range(process.pd, USER_OFFSET + PAGESIZE, 1, true));
    directory.writable = 1;
    page_table_entry &entry = process.pd->page_tables[VADDR_PD_OFFSET(USER_OFFSET)][1];
    entry.user = 0;
    CHECK(!vmm.user_range(process.pd, USER_OFFSET + PAGESIZE - 1, 2));
    entry.user = 1;
    CHECK(!vmm.user_range(process.pd, USER_STACK_BASE - 1, 2));
    CHECK(!vmm.get_physical_addr(process.pd, USER_IMAGE_LIMIT));
    CHECK(!vmm.get_physical_addr(process.pd, 0));
    CHECK(!(process.regs.esp & 15));
    const u32 *stack = (const u32 *)vmm.get_physical_addr(process.pd, process.regs.esp);
    CHECK(stack[0] == 2 && !stack[3] && !stack[4]);
    CHECK(!strcmp((const char *)vmm.get_physical_addr(process.pd, stack[1]), "init"));
    CHECK(!strcmp((const char *)vmm.get_physical_addr(process.pd, stack[2]), "second"));
    for (u32 i = 0; i < 3 * PAGESIZE; ++i) {
        u32 physical = vmm.get_physical_addr(process.pd, USER_OFFSET + i);
        u8 expected = i < 16 ? i + 1 : (i >= PAGESIZE && i < PAGESIZE + 16 ? i - PAGESIZE + 33 : 0);
        CHECK(*(const u8 *)physical == expected);
    }
    vmm.destroy_page_directory(process.pd);
    CHECK(vmm.frames_used == frames);
    CHECK(!is_elf(nullptr, sizeof(image)));
    CHECK(rejected(0) && rejected(sizeof(Elf32_Ehdr) - 1) && rejected(0xffffffff));
    for (u32 i = 0; i < 7; ++i) {
        make_image()->e_ident[i] ^= 0x80;
        CHECK(rejected());
    }
    make_image()->e_machine = 62; CHECK(rejected());
    make_image()->e_type = 3; CHECK(rejected());
    make_image()->e_version = 2; CHECK(rejected());
    make_image()->e_ehsize = 0; CHECK(rejected());
    make_image()->e_phentsize = 0; CHECK(rejected());
    make_image()->e_phoff = 0xfffffff0; CHECK(rejected());
    make_image()->e_phoff = sizeof(image) - sizeof(Elf32_Phdr); CHECK(rejected());
    make_image()->e_phnum = 0; CHECK(rejected());
    make_image()->e_phnum = 129; CHECK(rejected());
    make_image()->e_entry = USER_OFFSET + PAGESIZE; CHECK(rejected());
    make_image()->e_entry = USER_OFFSET + 16; CHECK(rejected());
    for (u32 field = 0; field < 12; ++field) {
        Elf32_Ehdr *header = make_image();
        Elf32_Phdr *segment = (Elf32_Phdr *)(image + header->e_phoff);
        switch (field) {
        case 0: segment[0].p_filesz = PAGESIZE + 1; break;
        case 1: segment[0].p_offset = 0xfffff000; break;
        case 2: segment[0].p_vaddr = USER_OFFSET - PAGESIZE; break;
        case 3: segment[0].p_memsz = 0xffffffff; break;
        case 4: segment[0].p_vaddr = USER_IMAGE_LIMIT; break;
        case 5: segment[0].p_flags = 7; break;
        case 6: segment[0].p_flags = 1; break;
        case 7: segment[0].p_align = 3; break;
        case 8: ++segment[0].p_offset; break;
        case 9: segment[1].p_vaddr = USER_OFFSET; break;
        case 10: segment[1].p_type = 3; break;
        case 11: segment[0].p_flags = 0x15; break;
        }
        CHECK(rejected());
    }
    make_image();
    CHECK(rejected(sizeof(image), -1) && rejected(sizeof(image), 33) && rejected(sizeof(image), 1));
    char long_arg[257];
    memset(long_arg, 'a', sizeof(long_arg));
    long_arg[256] = 0;
    const char *bad_args[] = {long_arg};
    CHECK(rejected(sizeof(image), 1, bad_args));
    u32 head = 0;
    u32 frame;
    while ((frame = vmm.alloc_frame())) {
        *(u32 *)frame = head;
        head = frame;
    }
    for (u32 i = 0; i < 8; ++i) {
        CHECK(rejected());
        CHECK(head);
        frame = head;
        head = *(u32 *)head;
        vmm.free_frame(frame);
    }
    while (head) {
        frame = head;
        head = *(u32 *)head;
        vmm.free_frame(frame);
    }
    CHECK(vmm.frames_used == frames && current_directory == active);
    serial::write("[ELF] validation, mappings, stack and rollback verified\n");
    return true;
}
