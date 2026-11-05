#include "user.h"

static const u32 size = 7 * 1024 * 1024;
static const u32 page_size = 4096;

static unsigned char value(u32 offset) {
    return 1 + ((offset / page_size) % 123);
}

int main(void) {
    volatile unsigned char *memory = malloc(size);
    if (!memory || open("/dev/hdb", 0) >= 0) return 1;
    for (u32 offset = 0; offset < size; offset += page_size)
        memory[offset] = value(offset);
    int child = fork();
    if (child < 0) return 2;
    if (!child) {
        for (u32 offset = 0; offset < size; offset += page_size) {
            if (memory[offset] != value(offset)) return 3;
            memory[offset] = 255 - value(offset);
        }
        for (u32 offset = 0; offset < size; offset += page_size)
            if (memory[offset] != 255 - value(offset)) return 4;
        int fd = open("/etc/motd", 0);
        char *buffer = (char *)memory + page_size - 7;
        if (fd < 0 || read(fd, buffer, 14) != 14) return 5;
        return close(fd) ? 6 : 0;
    }
    int status = -1;
    if (waitpid(child, &status, 0) != child || status) return 7;
    for (u32 offset = 0; offset < size; offset += page_size)
        if (memory[offset] != value(offset)) return 8;
    volatile unsigned char *extra = malloc(6 * 1024 * 1024);
    if (!extra) return 9;
    for (u32 offset = 0; offset < 6 * 1024 * 1024; offset += page_size) {
        if (extra[offset]) return 10;
        extra[offset] = value(offset);
    }
    for (u32 offset = 0; offset < size; offset += page_size)
        if (memory[offset] != value(offset)) return 11;
    for (u32 offset = 0; offset < 6 * 1024 * 1024; offset += page_size)
        if (extra[offset] != value(offset)) return 12;
    puts("swap under memory pressure verified\n");
    return 0;
}
