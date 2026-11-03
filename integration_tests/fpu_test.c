#include "user.h"

static unsigned features;

static int collect(int child, int expected) {
    int status;
    return child > 0 && waitpid(child, &status, 0) == child && status == expected << 8;
}

static void set_vector(unsigned value) {
    unsigned words[4] = {value, value, value, value};
    __asm__ volatile("movups (%0), %%xmm0" :: "r"(words) : "memory");
}

static int check_vector(unsigned value) {
    unsigned words[4];
    __asm__ volatile("movups %%xmm0, (%0)" :: "r"(words) : "memory");
    return words[0] == value && words[1] == value && words[2] == value && words[3] == value;
}

static void set_mmx(unsigned value) {
    unsigned words[2] = {value, value};
    __asm__ volatile("movq (%0), %%mm0" :: "r"(words) : "memory");
}

static int check_mmx(unsigned value) {
    unsigned words[2];
    __asm__ volatile("movq %%mm0, (%0)" :: "r"(words) : "memory");
    return words[0] == value && words[1] == value;
}

static int clean_state(void) {
    unsigned short environment[14];
    __asm__ volatile("fnstenv %0; fldenv %0" : "=m"(environment) :: "memory");
    if (environment[0] != 0x037f || environment[2] || environment[4] != 0xffff) return 0;
    if (features & (1U << 25)) {
        unsigned mxcsr;
        __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
        if (mxcsr != 0x1f80 || !check_vector(0)) return 0;
    }
    if (features & (1U << 23)) {
        int valid = check_mmx(0);
        __asm__ volatile("emms");
        if (!valid) return 0;
    }
    return 1;
}

static int x87(void) {
    __asm__ volatile("fninit; fld1" ::: "st");
    int child = fork();
    if (child < 0) return 0;
    if (!child) {
        int inherited;
        __asm__ volatile("fistpl %0; fninit; fldz" : "=m"(inherited) :: "st");
        if (inherited != 1) exit(1);
        unsigned short control = 0x0b7f;
        __asm__ volatile("fldcw %0" :: "m"(control));
        for (volatile unsigned i = 0; i < 20000000; ++i) {}
        exit(0);
    }
    for (volatile unsigned i = 0; i < 20000000; ++i) {}
    if (!collect(child, 0)) return 0;
    int value;
    unsigned short control;
    __asm__ volatile("fistpl %0; fnstcw %1" : "=m"(value), "=m"(control) :: "st");
    return value == 1 && control == 0x037f;
}

static int vectors(void) {
    if (features & (1U << 25)) {
        set_vector(0x12345678);
        unsigned initial = 0x1f80;
        __asm__ volatile("ldmxcsr %0" :: "m"(initial));
        int child = fork();
        if (child < 0) return 0;
        if (!child) {
            if (!check_vector(0x12345678)) exit(1);
            set_vector(0x87654321);
            unsigned changed = 0x3f80;
            __asm__ volatile("ldmxcsr %0" :: "m"(changed));
            for (volatile unsigned i = 0; i < 20000000; ++i) {}
            exit(0);
        }
        for (volatile unsigned i = 0; i < 20000000; ++i) {}
        if (!collect(child, 0) || !check_vector(0x12345678)) return 0;
        unsigned actual;
        __asm__ volatile("stmxcsr %0" : "=m"(actual));
        if (actual != initial) return 0;
    }
    if (features & (1U << 23)) {
        set_mmx(0x12345678);
        int child = fork();
        if (child < 0) return 0;
        if (!child) {
            if (!check_mmx(0x12345678)) exit(1);
            set_mmx(0x87654321);
            exit(0);
        }
        if (!collect(child, 0) || !check_mmx(0x12345678)) return 0;
        __asm__ volatile("emms");
    }
    return 1;
}

static int pending_exception(void) {
    int child = fork();
    if (child < 0) return 0;
    if (!child) {
        int zero = 0;
        unsigned short control = 0x037b, status;
        __asm__ volatile("fninit; fld1; fldcw %1; fidivl %0" :: "m"(zero), "m"(control) : "st");
        __asm__ volatile("fnstsw %0" : "=a"(status));
        if (!(status & 4)) exit(1);
        for (volatile unsigned i = 0; i < 20000000; ++i) {}
        __asm__ volatile("fnstsw %0" : "=a"(status));
        if (!(status & 4)) exit(2);
        __asm__ volatile("fnclex");
        exit(0);
    }
    return collect(child, 0);
}

int main(int argc, char **argv) {
    unsigned eax = 1, ebx, ecx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(features));
    if (!clean_state()) return 1;
    if (argc == 2 && argv[1][0] == 'c') return 42;
    if (!x87() || !vectors() || !pending_exception()) return 2;
    int child = fork();
    if (child < 0) return 3;
    if (!child) {
        unsigned short control = 0x0b7f;
        __asm__ volatile("fldcw %0; fld1" :: "m"(control) : "st");
        if (features & (1U << 25)) set_vector(0xffffffff);
        if (features & (1U << 23)) set_mmx(0xffffffff);
        const char *arguments[] = {"fpu-exec", "check", 0};
        execve("/bin/fpu-exec", arguments, 0);
        exit(4);
    }
    if (!collect(child, 42)) return 5;
    puts("floating-point state verified\n");
    return 0;
}
