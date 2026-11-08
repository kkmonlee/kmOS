#include "user.h"

int main(void) {
    for (int i = 0; i < 2; ++i) {
        const char *path = i ? "/mnt/disk/crash-b" : "/mnt/disk/crash-a";
        int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
        if (fd < 0 || ftruncate(fd, 128 * 1024) || write(fd, "crash", 5) != 5 || unlink(path))
            return 1;
    }
    puts("open orphans ready\n");
    for (;;) __asm__ volatile("" ::: "memory");
}
