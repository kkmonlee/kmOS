#include "user.h"

static unsigned char buffer[1300];

int main(void) {
    if (open("/dev/hda", 0) >= 0) return 1;
    const char *path = "/mnt/disk/note";
    int fd = open(path, 1);
    if (fd >= 0) {
        for (u32 i = 0; i < 1028; ++i) buffer[i] = 33 + i % 90;
        int result = write(fd, buffer, 1028);
        if (result == -5) {
            puts("disk write rejected\n");
            return 0;
        }
        if (result != 1028 || write(fd, buffer, 273) != -27 || close(fd))
            return 2;
    } else if (fd != -30) {
        return 4;
    }
    fd = open(path, 0);
    if (fd < 0 || read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) return 5;
    for (u32 i = 0; i < sizeof(buffer); ++i) {
        unsigned char expected = i < 1028 ? 33 + i % 90 : 0x5a;
        if (buffer[i] != expected) return 6;
    }
    if (close(fd)) return 7;
    puts("disk contents verified\n");
    return 0;
}
