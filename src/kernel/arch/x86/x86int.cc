#include <os.h>
#include <vmm.h>
#include <serial.h>
#include <architecture.h>

extern "C" void test_gp();
extern "C" char test_gp_fault, test_gp_resume;
static volatile u32 testing_exception;
static volatile u32 page_fault_count;

extern "C" u32 get_page_fault_count() { return page_fault_count; }

extern "C" InterruptFrame *exception_handler(regs_t *frame) {
    if (testing_exception == 3 && frame->which_int == 3 && frame->err_code == 0 &&
        frame->eax == 0x12345678 && frame->ebx == 0x87654321 &&
        frame->cs == 0x08 && frame->ds == 0x10) {
        frame->eax = 0xabcdef01;
        testing_exception = 0;
        return nullptr;
    }
    if (testing_exception == 13 && frame->which_int == 13 &&
        frame->err_code == 0xfff8 && frame->eip == (u32)&test_gp_fault) {
        frame->eip = (u32)&test_gp_resume;
        testing_exception = 0;
        return nullptr;
    }
    u32 address = 0;
    if (frame->which_int == 14) {
        ++page_fault_count;
        asm volatile("mov %%cr2, %0" : "=r"(address));
        if (vmm.handle_page_fault(address, frame->err_code) == 0) return nullptr;
    }
    if ((frame->cs & 3) == 3 && arch.pcurrent) {
        serial::write("[USER] fault vector=");
        serial::hex(frame->which_int);
        serial::write(" address=");
        serial::hex(address);
        serial::write("\n");
        return arch.finish_process(128 + frame->which_int, frame->which_int);
    }
    serial::write("[PANIC] exception=");
    serial::hex(frame->which_int);
    serial::write(" error=");
    serial::hex(frame->err_code);
    serial::write(" eip=");
    serial::hex(frame->eip);
    if (frame->which_int == 14) {
        serial::write(" address=");
        serial::hex(address);
    }
    serial::write("\n");
    io.print("Kernel exception %d, error %x, eip %x\n", frame->which_int, frame->err_code, frame->eip);
    for (;;) asm volatile("cli; hlt");
}

extern "C" bool verify_exception_frame() {
    testing_exception = 3;
    u32 result = 0x12345678;
    asm volatile("int3" : "+a"(result) : "b"(0x87654321) : "memory", "cc");
    if (testing_exception || result != 0xabcdef01) return false;
    testing_exception = 13;
    test_gp();
    return testing_exception == 0;
}
