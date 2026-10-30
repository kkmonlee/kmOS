#include "user.h"

#define CHECK(test) do { if (!(test)) fail(__LINE__); } while (0)
static volatile int shared = 0x12345678;
static volatile char destination[8] = {'Q'};

static void number(int value, char *text) {
    char reversed[12];
    int length = 0;
    do { reversed[length++] = '0' + value % 10; value /= 10; } while (value);
    int i = 0;
    while (length) text[i++] = reversed[--length];
    text[i] = 0;
}

static void fail(int line) {
    char text[12];
    number(line, text);
    puts("process test failed at ");
    puts(text);
    puts("\n");
    exit(1);
}

static void expect(int child, int status) {
    int value = -1;
    CHECK(child > 0);
    CHECK(waitpid(child, &value, 0) == child);
    CHECK(value == status);
    CHECK(waitpid(child, &value, 0) == -10);
}

static void descriptors(void) {
    char value;
    int first = open("/etc/motd", 0);
    int second = open("/etc/motd", 0);
    CHECK(first == 3 && second == 4);
    CHECK(read(first, &value, 1) == 1 && value == 'k');
    CHECK(read(second, &value, 1) == 1 && value == 'k');
    CHECK(dup2(first, 10) == 10);
    CHECK(read(10, &value, 1) == 1 && value == 'm');
    CHECK(read(first, &value, 1) == 1 && value == 'O');
    CHECK(close(first) == 0 && close(first) == -9);
    CHECK(dup2(10, 10) == 10);
    CHECK(read(10, &value, 1) == 1 && value == 'S');
    CHECK(dup2(31, 10) == -9);
    CHECK(dup2(10, second) == second);
    CHECK(read(second, &value, 1) == 1 && value == ' ');
    CHECK(close(10) == 0);
    CHECK(read(second, &value, 1) == 1 && value == 'u');
    CHECK(write(second, &value, 1) == -9);
    CHECK(read(1, &value, 1) == -9);
    CHECK(close(second) == 0);
    CHECK(close(32) == -9 && dup2(1, 32) == -9);
    CHECK(open("/etc/motd", 8) == -22);
    CHECK(open("/etc/motd", 1) == -30);
    CHECK(open("/missing", 0) == -2);
    int files[32], count = 0, fd;
    while ((fd = open("/etc/motd", 0)) >= 0) files[count++] = fd;
    CHECK(fd == -24 && count == 29);
    while (count) CHECK(close(files[--count]) == 0);
    CHECK(open("/etc/motd", 0) == 3);
    CHECK(close(3) == 0);
}

static void isolation(void) {
    int fd = open("/etc/motd", 0);
    CHECK(fd == 3);
    int child = fork();
    CHECK(child >= 0);
    if (!child) {
        CHECK(shared == 0x12345678 && destination[0] == 'Q');
        shared = 0x87654321;
        CHECK(read(fd, (void *)destination, 1) == 1 && destination[0] == 'k');
        CHECK(close(fd) == 0);
        exit(23);
    }
    expect(child, 23 << 8);
    CHECK(shared == 0x12345678 && destination[0] == 'Q');
    char value;
    CHECK(read(fd, &value, 1) == 1 && value == 'm');
    CHECK(close(fd) == 0);
    CHECK(chdir("/etc") == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        CHECK(open("motd", 0) == 3 && close(3) == 0);
        CHECK(chdir("/bin") == 0);
        CHECK(open("hello", 0) == 3 && close(3) == 0);
        exit(0);
    }
    expect(child, 0);
    CHECK(open("motd", 0) == 3 && close(3) == 0);
    CHECK(chdir("/") == 0);
}

static void children(void) {
    int status = 777;
    CHECK(waitpid(-1, &status, 0) == -10 && status == 777);
    CHECK(waitpid(0, &status, 0) == -22);
    CHECK(waitpid(-1, &status, 2) == -22);
    int first = fork();
    CHECK(first >= 0);
    if (!first) { for (volatile int i = 0; i < 50000000; ++i) {} exit(10); }
    CHECK(waitpid(first, &status, 1) == 0 && status == 777);
    int second = fork();
    CHECK(second >= 0);
    if (!second) { CHECK(waitpid(first, 0, 0) == -10); exit(11); }
    expect(second, 11 << 8);
    expect(first, 10 << 8);
    first = fork();
    CHECK(first >= 0);
    if (!first) exit(12);
    second = fork();
    CHECK(second >= 0);
    if (!second) exit(13);
    int result = waitpid(-1, &status, 0);
    CHECK(result == first || result == second);
    CHECK(status == ((result == first ? 12 : 13) << 8));
    expect(result == first ? second : first, (result == first ? 13 : 12) << 8);
    CHECK(waitpid(-1, 0, 1) == -10);
    int *crossing = (int *)0xdfffcfff;
    *crossing = 777;
    first = fork();
    CHECK(first >= 0);
    if (!first) { CHECK(*crossing == 777); exit(37); }
    CHECK(waitpid(first, crossing, 0) == first && *crossing == (37 << 8));
}

static void argument_limits(void) {
    char long_argument[257];
    for (int i = 0; i < 256; ++i) long_argument[i] = 'a';
    long_argument[256] = 0;
    const char *argv[] = {long_argument, 0};
    CHECK(execve("/bin/hello", argv, 0) == -7);
    const char *many[34];
    for (int i = 0; i < 33; ++i) many[i] = "argument";
    many[33] = 0;
    CHECK(execve("/bin/hello", many, 0) == -7);
    long_argument[200] = 0;
    for (int i = 0; i < 32; ++i) many[i] = long_argument;
    many[32] = 0;
    CHECK(execve("/bin/hello", many, 0) == -7);
    static char long_path[4097];
    for (int i = 0; i < 4096; ++i) long_path[i] = '/';
    long_path[4096] = 0;
    CHECK(execve(long_path, 0, 0) == -36);
}

static void execution(void) {
    int parent = getpid();
    const char *bad[] = {"bad", 0};
    CHECK(execve("/missing", bad, 0) == -2);
    CHECK(execve("/etc/motd", bad, 0) == -8);
    CHECK(execve("/bin", bad, 0) == -13);
    CHECK(shared == 0x12345678 && getpid() == parent);
    int normal = open("/etc/motd", 0);
    int cloexec = open("/etc/motd", 0x80000);
    CHECK(normal == 3 && cloexec == 4);
    CHECK(dup2(cloexec, 12) == 12);
    char value;
    CHECK(read(normal, &value, 1) == 1 && value == 'k');
    int child = fork();
    CHECK(child >= 0);
    if (!child) {
        char fd_text[12], clo_text[12], pid_text[12], parent_text[12];
        number(normal, fd_text); number(cloexec, clo_text);
        number(getpid(), pid_text); number(parent, parent_text);
        char *marker = (char *)0xdfffdffd;
        for (int i = 0; i < 7; ++i) marker[i] = "marker"[i];
        const char **argv = (const char **)0xdfffcffc;
        argv[0] = "exec-target"; argv[1] = marker; argv[2] = fd_text;
        argv[3] = clo_text; argv[4] = pid_text; argv[5] = parent_text; argv[6] = 0;
        const char *envp[] = {"KEY=value", "SECOND=two", 0};
        CHECK(execve("/bin/exec-target", argv, envp) < 0);
        fail(__LINE__);
    }
    expect(child, 42 << 8);
    CHECK(read(normal, &value, 1) == 1 && value == 'O');
    CHECK(read(cloexec, &value, 1) == 1 && value == 'm');
    CHECK(close(normal) == 0 && close(cloexec) == 0 && close(12) == 0);
    CHECK(getpid() == parent);
}

static void faults(void) {
    for (int which = 0; which < 7; ++which) {
        int child = fork();
        CHECK(child >= 0);
        if (!child) {
            if (which == 0) *(volatile int *)0x100000 = 1;
            if (which == 1) __asm__ volatile("ud2");
            if (which == 2) __asm__ volatile("cli");
            if (which == 3) open((const char *)0x100000, 0);
            if (which == 4) { int fd = open("/etc/motd", 0); read(fd, (void *)0x40000000, 1); }
            if (which == 5) execve("/bin/hello", (const char *const *)0x100000, 0);
            if (which == 6) {
                int grandchild = fork();
                CHECK(grandchild >= 0);
                if (!grandchild) exit(0);
                waitpid(grandchild, (int *)0x100000, 0);
            }
            fail(__LINE__);
        }
        expect(child, which == 1 ? 6 : (which == 2 ? 13 : 14));
    }
}

static void repeated(void) {
    for (int i = 0; i < 16; ++i) {
        int child = fork();
        CHECK(child >= 0);
        if (!child) {
            const char *argv[] = {"hello", 0};
            execve("/bin/hello", argv, 0);
            exit(127);
        }
        expect(child, 0);
    }
    int children[64], count = 0, child;
    while ((child = fork()) > 0) children[count++] = child;
    if (!child) exit(19);
    CHECK(count > 0 && (child == -11 || child == -12));
    while (count) expect(children[--count], 19 << 8);
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        int grandchild = fork();
        CHECK(grandchild >= 0);
        if (!grandchild) { for (volatile int i = 0; i < 5000000; ++i) {} exit(0); }
        exit(9);
    }
    expect(child, 9 << 8);
}

int main(void) {
    CHECK(getpid() > 1 && getppid() == 0);
    descriptors();
    isolation();
    children();
    argument_limits();
    execution();
    faults();
    repeated();
    puts("process lifecycle verified\n");
    return 0;
}
