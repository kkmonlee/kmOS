#include "user.h"

int main(void) {
    int count = 0;
    for (int i = 0; i < 100; ++i) {
        char path[] = "/mnt/disk/inode-00";
        path[sizeof(path) - 3] = '0' + i / 10;
        path[sizeof(path) - 2] = '0' + i % 10;
        int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
        if (fd == -28 || fd == -30) break;
        if (fd == -17) fd = open(path, O_RDONLY);
        if (fd < 0 || close(fd)) return 1;
        ++count;
    }
    if (count != 21) return 2;
    puts("inode exhaustion verified\n");
    return 0;
}
