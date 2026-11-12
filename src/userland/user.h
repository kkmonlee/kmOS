#ifndef USER_H
#define USER_H

#include "../sdk/include/fcntl.h"
#include "../sdk/include/signal.h"

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
static inline int open(const char *path, int flags, ...) {
    u32 mode = 0;
    if (flags & O_CREAT) {
        __builtin_va_list arguments;
        __builtin_va_start(arguments, flags);
        mode = __builtin_va_arg(arguments, u32);
        __builtin_va_end(arguments);
    }
    return syscall3(5, (u32)path, flags, mode);
}
static inline int mkdir(const char *path, u32 mode) { return syscall3(15, (u32)path, mode, 0); }
static inline int rmdir(const char *path) { return syscall3(40, (u32)path, 0, 0); }
static inline int unlink(const char *path) { return syscall3(17, (u32)path, 0, 0); }
static inline int ftruncate(int fd, int size) { return syscall3(93, fd, size, 0); }
static inline int lseek(int fd, int offset, int origin) { return syscall3(19, fd, offset, origin); }
static inline int close(int fd) { return syscall3(6, fd, 0, 0); }
static inline int read(int fd, void *buffer, u32 count) { return syscall3(3, fd, (u32)buffer, count); }
static inline int write(int fd, const void *buffer, u32 count) { return syscall3(4, fd, (u32)buffer, count); }
static inline int dup2(int from, int to) { return syscall3(63, from, to, 0); }
static inline int getpid(void) { return syscall3(20, 0, 0, 0); }
static inline int getppid(void) { return syscall3(64, 0, 0, 0); }
static inline int chdir(const char *path) { return syscall3(12, (u32)path, 0, 0); }
static inline int pipe(int fds[2]) { return syscall3(42, (u32)fds, 0, 0); }
static inline int kill(int pid, int signal) { return syscall3(37, pid, signal, 0); }
extern void __kmos_sigreturn(void);

static inline int sigaction(int signal, const struct sigaction *action, struct sigaction *previous) {
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(67), "b"(signal), "c"(action), "d"(previous), "S"(__kmos_sigreturn)
                     : "memory", "cc");
    return result;
}

static inline int setpgid(int pid, int group) { return syscall3(57, pid, group, 0); }
static inline int set_foreground(int group) { return syscall3(54, 0, 1, group); }

static inline void *sbrk(int increment) {
    u32 result = syscall3(45, increment, 0, 0);
    return (void *)(result >= 0xfffff000U ? (u32)-1 : result);
}

void *malloc(u32 size);
void *calloc(u32 count, u32 size);
void *realloc(void *memory, u32 size);
void free(void *memory);

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
