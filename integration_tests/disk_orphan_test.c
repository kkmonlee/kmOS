#include "user.h"

int main(void) {
    for (int i = 0; i < 2; ++i) {
        const char *path = i ? "/mnt/disk/crash-b" : "/mnt/disk/crash-a";
        int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
        int offset = (12 + 256 + i * 256) * 1024;
        if (fd < 0 || lseek(fd, offset, SEEK_SET) != offset || write(fd, "crash", 5) != 5 || unlink(path))
            return 1;
    }
    puts("open orphans ready\n");
    for (;;) __asm__ volatile("" ::: "memory");
}
