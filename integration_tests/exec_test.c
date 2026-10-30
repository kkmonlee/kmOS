#include "user.h"

static int integer(const char *text) {
    int value = 0;
    while (*text) value = value * 10 + *text++ - '0';
    return value;
}

static int equal(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static volatile int initialized = 17;
static volatile int zero;

int main(int argc, char **argv, char **envp) {
    if (argc != 6 || !equal(argv[0], "exec-target") || !equal(argv[1], "marker") || argv[6]) return 1;
    if (!equal(envp[0], "KEY=value") || !equal(envp[1], "SECOND=two") || envp[2]) return 2;
    if (getpid() != integer(argv[4]) || getppid() != integer(argv[5])) return 3;
    if (initialized != 17 || zero) return 4;
    char value;
    if (read(integer(argv[3]), &value, 1) != -9) return 5;
    if (read(12, &value, 1) != 1 || value != 'k') return 6;
    if (read(integer(argv[2]), &value, 1) != 1 || value != 'm') return 7;
    return 42;
}
