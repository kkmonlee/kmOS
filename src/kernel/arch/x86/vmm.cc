#include <os.h>
#include <vmm.h>

void init_vmm() {
    io.print("[VMM] Initializing virtual memory manager\n");
}

char *get_page_frame() {
    return (char*)0x100000;
}

void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags) {
}

void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd) {
}