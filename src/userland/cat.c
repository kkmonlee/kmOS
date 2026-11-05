#include "user.h"

static int copy(int fd) {
    char buffer[512];
    int count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0)
        if (write_all(1, buffer, count)) return 1;
    if (count < 0) {
        static const char error[] = "cat: read failed\n";
        write_all(2, error, sizeof(error) - 1);
    }
    return count < 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return copy(0);
    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            static const char error[] = "cat: open failed\n";
            write_all(2, error, sizeof(error) - 1);
            return 1;
        }
        int result = copy(fd);
        close(fd);
        if (result) return result;
    }
    return 0;
}
