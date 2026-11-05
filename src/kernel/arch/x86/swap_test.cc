#include <os.h>
#include <cow.h>
#include <serial.h>
#include <swap.h>

class TestSwapFile : public File {
public:
    TestSwapFile(const char *name) : File(name, TYPE_FILE) { setSize(2 * PAGESIZE); }
    u32 open(u32) override { return 0; }
    u32 close() override { return 0; }

    u32 read(u32 pos, u8 *buffer, u32 count) override {
        if (!pos && count == 512) {
            const char header[] = "KMOS-SWAP";
            for (u32 i = 0; i < sizeof(header); ++i) buffer[i] = header[i];
            return count;
        }
        if (pos != PAGESIZE || count != PAGESIZE || fail_read) return (u32)-5;
        for (u32 i = 0; i < count; ++i) buffer[i] = data[i];
        return count;
    }

    u32 write(u32 pos, u8 *buffer, u32 count) override {
        if (pos != PAGESIZE || count != PAGESIZE || fail_write) return (u32)-5;
        for (u32 i = 0; i < count; ++i) data[i] = buffer[i];
        return count;
    }

    bool fail_read = false;
    bool fail_write = false;

private:
    u8 data[PAGESIZE];
};

bool verify_swap() {
    const u32 base = USER_OFFSET;
    const u32 before = vmm.frames_used;
    page_directory *parent = vmm.create_page_directory();
    page_directory *child = vmm.create_page_directory();
    if (!parent || !child ||
        cow_manager.map_pages(parent, base, base + 3 * PAGESIZE, PG_PRESENT | PG_USER | PG_WRITE))
        return false;
    u32 first = vmm.get_physical_addr(parent, base);
    *(u8 *)(first + PAGESIZE - 1) = 'a';
    *(u8 *)vmm.get_physical_addr(parent, base + PAGESIZE) = 'b';
    vmm.get_page_table(parent, base, 0)[0].writable = 0;

    TestSwapFile *a = new TestSwapFile("swap-a");
    TestSwapFile *b = new TestSwapFile("swap-b");
    if (!a || !b || !swap_manager.add_file(a)) return false;
    a->fail_write = true;
    if (swap_manager.swap_out_page(parent, base) ||
        vmm.get_physical_addr(parent, base) != first ||
        swap_manager.get_swap_devices()->inuse_pages) return false;
    a->fail_write = false;
    if (!swap_manager.swap_out_page(parent, base) || !swap_manager.add_file(b) ||
        !swap_manager.swap_out_page(parent, base + PAGESIZE) ||
        swap_manager.swap_out_page(parent, base + 2 * PAGESIZE) ||
        swap_manager.remove_file("swap-a")) return false;

    if (cow_manager.cow_copy_page_range(child, parent, base, base + 3 * PAGESIZE)) return false;
    a->fail_read = true;
    char data[2];
    int error;
    u32 used = vmm.frames_used;
    if (vmm.copy_from_user(child, data, base + PAGESIZE - 1, sizeof(data), &error) ||
        error != -5 || vmm.frames_used != used) return false;
    a->fail_read = false;
    if (!vmm.copy_from_user(child, data, base + PAGESIZE - 1, sizeof(data)) ||
        data[0] != 'a' || data[1] != 'b' ||
        vmm.copy_to_user(child, base, data, 1) ||
        !vmm.copy_to_user(child, base + PAGESIZE, "c", 1)) return false;

    vmm.switch_page_directory(parent);
    bool valid = vmm.handle_page_fault(base, 4) == 0 &&
        *(const char *)(base + PAGESIZE - 1) == 'a' &&
        !vmm.user_write_range(parent, base, 1);
    vmm.switch_page_directory(kernel_directory);
    if (!valid || !swap_manager.remove_file("swap-a") ||
        !vmm.copy_from_user(parent, data, base + PAGESIZE, 1) || data[0] != 'b' ||
        !swap_manager.swap_out_page(child, base + PAGESIZE)) return false;
    vmm.destroy_page_directory(child);
    vmm.destroy_page_directory(parent);
    if (!swap_manager.remove_file("swap-b") || vmm.frames_used != before) return false;
    serial::write("[SWAP] Slot ownership, protection and I/O errors verified\n");
    return true;
}
