#include "user.h"

int main(void) {
    if (mkdir("/mnt/disk/crash-parent", 0755) ||
        mkdir("/mnt/disk/crash-parent/child", 0755) ||
        chdir("/mnt/disk/crash-parent/child") ||
        rmdir("/mnt/disk/crash-parent/child") ||
        rmdir("/mnt/disk/crash-parent")) return 1;
    puts("directory orphans ready\n");
    for (;;) __asm__ volatile("" ::: "memory");
}
