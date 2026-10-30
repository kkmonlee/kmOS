#ifndef USER_H
#define USER_H

typedef unsigned int u32;

static inline int syscall3(int number, u32 first, u32 second, u32 third) {
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(first), "c"(second), "d"(third) : "memory", "cc");
    return result;
}

static inline int fork(void) { return syscall3(2, 0, 0, 0); }
static inline int waitpid(int pid, int *status, int flags) { return syscall3(7, pid, (u32)status, flags); }
static inline int execve(const char *path, const char *const *argv, const char *const *envp) {
    return syscall3(11, (u32)path, (u32)argv, (u32)envp);
}
static inline int open(const char *path, int flags) { return syscall3(5, (u32)path, flags, 0); }
static inline int close(int fd) { return syscall3(6, fd, 0, 0); }
static inline int read(int fd, void *buffer, u32 count) { return syscall3(3, fd, (u32)buffer, count); }
static inline int write(int fd, const void *buffer, u32 count) { return syscall3(4, fd, (u32)buffer, count); }
static inline int dup2(int from, int to) { return syscall3(63, from, to, 0); }
static inline int getpid(void) { return syscall3(20, 0, 0, 0); }
static inline int getppid(void) { return syscall3(64, 0, 0, 0); }
static inline int chdir(const char *path) { return syscall3(12, (u32)path, 0, 0); }
static inline void exit(int status) {
    syscall3(1, status, 0, 0);
    for (;;) __asm__ volatile("ud2");
}

static inline int write_all(int fd, const void *buffer, u32 count) {
    const char *bytes = buffer;
    while (count) {
        int result = write(fd, bytes, count);
        if (result <= 0) return -1;
        bytes += result;
        count -= result;
    }
    return 0;
}

static inline int puts(const char *text) {
    u32 count = 0;
    while (text[count]) ++count;
    return write_all(1, text, count);
}

#endif
