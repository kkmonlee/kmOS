#include "user.h"

static int run(const char *path, const char *const *argv) {
    int child = fork();
    if (child < 0) return 1;
    if (!child) {
        execve(path, argv, 0);
        puts("init: exec failed\n");
        exit(127);
    }
    int status;
    return waitpid(child, &status, 0) != child || status != 0;
}

int main(void) {
    const char *hello[] = {"hello", 0};
    const char *cat[] = {"cat", "/etc/motd", 0};
    puts("Starting userland\n");
    if (run("/bin/hello", hello) || run("/bin/cat", cat)) return 1;
    return 0;
}
