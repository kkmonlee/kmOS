#include "user.h"

#define CHECK(test) do { if (!(test)) { puts("failed: " #test "\n"); return 1; } } while (0)

int main(void) {
    CHECK(chdir("/mnt/disk") == 0);
    int fd = open("create/new", O_RDONLY);
    if (fd == -2) {
        fd = open("create/new", O_CREAT | O_EXCL | O_RDWR, 0644);
        CHECK(fd >= 0 && write(fd, "x", 1) == 1 && close(fd) == 0);
        CHECK(mkdir("mkdir/new", 0755) == 0);
        CHECK(unlink("unlink/old") == 0);
        CHECK(rmdir("rmdir/empty") == 0);
        CHECK(rmdir("keep/busy") == -39);
        puts("indexed ext2 directories modified\n");
        return 0;
    }

    char byte;
    CHECK(fd >= 0 && read(fd, &byte, 1) == 1 && byte == 'x' && close(fd) == 0);
    fd = open("create/original", O_RDONLY);
    CHECK(fd >= 0 && read(fd, &byte, 1) == 1 && byte == 's' && close(fd) == 0);
    CHECK(chdir("mkdir/new") == 0 && chdir("../..") == 0);
    CHECK(open("unlink/old", O_RDONLY) == -2 && chdir("rmdir/empty") == -2);
    CHECK(mkdir("keep/new", 0755) == -30);
    puts("indexed ext2 directory changes verified after reboot\n");
    return 0;
}
