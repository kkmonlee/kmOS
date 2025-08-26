#include <os.h>
#include <architecture.h>
#include <vmm.h>

extern "C" {
    // enable_paging() handled by VMM
}

// Serial debugging functions
static void serial_outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static void serial_print(const char* str) {
    while (*str) {
        serial_outb(0x3F8, *str);
        str++;
    }
}

void Architecture::init() {
    serial_print("[ARCH] Starting x86 architecture initialization\n");
    
    io.print("[ARCH] Initializing x86 architecture\n");
    
    serial_print("[ARCH] About to initialize VMM\n");
    // Initialize virtual memory management
    vmm.init();
    
    serial_print("[ARCH] VMM initialized (paging handled by VMM)\n");
    io.print("[ARCH] x86 architecture initialization complete\n");
}

void Architecture::enable_interrupt() {
    io.print("[ARCH] Enabling interrupts\n");
}

char* Architecture::detect() {
    return (char*)"Generic x86";
}

void Architecture::addProcess(Process* p) {
    pcurrent = p;
}

u32 Architecture::getArg(u32 /*argnum*/) {
    return 0;
}

int Architecture::createProc(process_st* /*info*/, char* /*file*/, int /*argc*/, char** /*argv*/) {
    return 0;
}

void Architecture::disable_interrupt() {
}

void Architecture::setRet(u32 value) {
    (void)value;
}

void Architecture::change_process_father(Process* child, Process* new_father) {
    (void)child;
    (void)new_father;
}

struct page_directory *Architecture::get_current_page_directory() {
    return current_directory;
}

void Architecture::destroy_all_zombie() {
}