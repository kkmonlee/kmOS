#include "user.h"

int main(void) {
    int root = open("/mnt/disk", O_RDONLY | O_DIRECTORY);
    if (root < 0 || close(root)) return 1;
    for (int i = 0; i < 2; ++i) {
        const char *path = i ? "/mnt/disk/crash-b" : "/mnt/disk/crash-a";
        int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
        if (!i && fd == -30) {
            puts("read-only orphan mount closes normally\n");
            return 0;
        }
        int offset = (12 + 256 + i * 256) * 1024;
        if (fd < 0 || lseek(fd, offset, SEEK_SET) != offset || write(fd, "crash", 5) != 5 || unlink(path))
            return 1;
    }
    puts("open orphans ready\n");
    for (;;) __asm__ volatile("" ::: "memory");
}
