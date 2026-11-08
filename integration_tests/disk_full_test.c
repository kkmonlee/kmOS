#include "user.h"

static unsigned char buffer[4096];

int main(void) {
    for (u32 i = 0; i < sizeof(buffer); ++i) buffer[i] = 0x6c;
    int fd = open("/mnt/disk/filled", O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd >= 0) {
        int result;
        while ((result = write(fd, buffer, sizeof(buffer))) == sizeof(buffer)) {}
        if (result != -28) return 1;
        while ((result = write(fd, buffer, 512)) == 512) {}
        if (result != -28 || close(fd)) return 2;
    } else if (fd != -30 && fd != -17) {
        return 3;
    }
    fd = open("/mnt/disk/filled", O_RDONLY);
    if (fd < 0) return 4;
    int size = lseek(fd, 0, SEEK_END);
    if (size <= 0 || lseek(fd, 0, SEEK_SET)) return 5;
    int total = 0, count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < count; ++i) if (buffer[i] != 0x6c) return 6;
        total += count;
    }
    if (count || total != size || close(fd)) return 7;
    puts("full disk data verified\n");
    return 0;
}
