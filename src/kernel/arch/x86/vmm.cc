#include <os.h>
#include <vmm.h>

void init_vmm() {
    io.print("[VMM] Initializing virtual memory manager\n");
}

char *get_page_frame() {
    return (char*)0x100000;
}

extern "C" {
void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags) {
    // For now, just log the mapping (in a real kernel this would set up page tables)
    (void)v_addr;
    (void)p_addr;
    (void)flags;
    // In a minimal implementation, we assume identity mapping is already set up
}

void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd) {
    (void)v_addr;
    (void)p_addr;
    (void)flags;
    (void)pd;
}
}