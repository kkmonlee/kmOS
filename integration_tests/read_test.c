#include "user.h"

static char buffer[12 * 1024 * 1024];

int main(void) {
    int fd = open("/etc/motd", 0);
    if (fd < 0) return 1;
    int child = fork();
    if (child < 0) return 2;
    if (!child) {
        const char expected[] = "kmOS userland\n";
        if (read(fd, buffer, sizeof(buffer)) != sizeof(expected) - 1) exit(3);
        for (unsigned i = 0; i < sizeof(expected) - 1; ++i)
            if (buffer[i] != expected[i]) exit(4);
        if (buffer[sizeof(expected) - 1] || buffer[sizeof(buffer) - 1]) exit(5);
        if (read(fd, buffer, sizeof(buffer)) != 0) exit(6);
        exit(0);
    }
    int status;
    if (waitpid(child, &status, 0) != child || status) return 7;
    if (buffer[0] || buffer[sizeof(buffer) - 1]) return 8;
    if (close(fd)) return 9;
    puts("large read and COW verified\n");
    return 0;
}
