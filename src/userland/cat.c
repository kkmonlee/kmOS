#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: cat file...\n");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], 0);
        if (fd < 0) { puts("cat: open failed\n"); return 1; }
        char buffer[512];
        int count;
        while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
            if (write_all(1, buffer, count)) { close(fd); return 1; }
        }
        close(fd);
        if (count < 0) { puts("cat: read failed\n"); return 1; }
    }
    return 0;
}
