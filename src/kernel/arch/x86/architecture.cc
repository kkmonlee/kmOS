#include <os.h>
#include <architecture.h>
#include <vmm.h>

extern "C" {
    void enable_paging();
}

void Architecture::init() {
    io.print("[ARCH] Initializing x86 architecture\n");
    
    // Initialize virtual memory management
    vmm.init();
    
    // Enable paging
    enable_paging();
    
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

void Architecture::destroy_all_zombie() {
}