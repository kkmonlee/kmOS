#include "user.h"

static int break_test(void) {
    volatile unsigned char *base = sbrk(0);
    if (base == (void *)-1 || (u32)base % 4096 || sbrk(8193) != base) return 1;
    if (base[0] || base[4096] || base[8192]) return 2;
    base[8192] = 0xa5;
    int child = fork();
    if (child < 0) return 3;
    if (!child) {
        if (sbrk(-1) != base + 8193 || sbrk(0) != base + 8192) return 4;
        return base[8192];
    }
    int status;
    if (waitpid(child, &status, 0) != child || status != 14 || base[8192] != 0xa5) return 5;
    if (sbrk(-8193) != base + 8193 || sbrk(-1) != (void *)-1 ||
        sbrk(0x7fffffff) != (void *)-1 || sbrk(128 * 1024 * 1024) != (void *)-1 ||
        sbrk(0) != base) return 6;
    if (sbrk(8193) != base || base[8192] || sbrk(-8193) != base + 8193) return 7;
    return 0;
}

int main(void) {
    int result = break_test();
    if (result) return result;
    unsigned char *a = malloc(128), *b = malloc(128), *guard = malloc(1);
    if (!a || !b || !guard || ((u32)a | (u32)b | (u32)guard) & 15) return 8;
    free(a);
    free(b);
    unsigned char *combined = calloc(32, 8);
    if (combined != a || calloc(0xffffffffU, 2)) return 9;
    for (u32 i = 0; i < 256; ++i) {
        if (combined[i]) return 10;
        combined[i] = i;
    }
    unsigned char *larger = realloc(combined, 8192);
    if (!larger || larger == combined || realloc(larger, 0xffffffffU)) return 11;
    for (u32 i = 0; i < 256; ++i) if (larger[i] != i) return 12;
    int child = fork();
    if (child < 0) return 13;
    if (!child) {
        larger[10] = 250;
        free(larger);
        return 0;
    }
    int status;
    if (waitpid(child, &status, 0) != child || status || larger[10] != 10) return 14;
    free(guard);
    free(larger);
    puts("userspace heap verified\n");
    return 0;
}
