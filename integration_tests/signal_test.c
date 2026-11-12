#include "user.h"

#define CHECK(test) do { if (!(test)) fail(#test); } while (0)

static volatile int last_signal;
static volatile int term_count;
static volatile int handler_child;

static void fail(const char *test) {
    puts("signal test failed: ");
    puts(test);
    puts("\n");
    exit(1);
}

static void record_signal(int signal) { last_signal = signal; }

static void queue_signals(int signal) {
    CHECK(signal == SIGTERM);
    if (++term_count != 1) return;
    CHECK(kill(getpid(), SIGINT) == 0 && last_signal == 0);
    CHECK(kill(getpid(), SIGTERM) == 0 && kill(getpid(), SIGTERM) == 0 && term_count == 1);
}

static void nested_signal(int signal) {
    CHECK(signal == SIGTERM && kill(getpid(), SIGINT) == 0 && last_signal == SIGINT);
    last_signal = SIGTERM;
}

static void change_fpu(int signal) {
    CHECK(signal == SIGTERM);
    __asm__ volatile("fninit; fldz" ::: "st");
}

static void fork_in_handler(int signal) {
    CHECK(signal == SIGTERM);
    handler_child = fork();
}

static void handlers(void) {
    struct sigaction action = {record_signal, 0, 0};
    struct sigaction previous;
    CHECK(sigaction(SIGKILL, &action, 0) == -22 && syscall3(68, 0, 0, 0) == -22);
    CHECK(sigaction(SIGINT, &action, &previous) == 0 && previous.sa_handler == SIG_DFL);
    action = (struct sigaction){queue_signals, 1U << (SIGINT - 1), 0};
    CHECK(sigaction(SIGTERM, &action, 0) == 0);
    CHECK(kill(getpid(), SIGTERM) == 0 && term_count == 2 && last_signal == SIGINT);

    action = (struct sigaction){nested_signal, 0, 0};
    last_signal = 0;
    CHECK(sigaction(SIGTERM, &action, &previous) == 0 && previous.sa_handler == queue_signals);
    CHECK(previous.sa_mask == (1U << (SIGINT - 1)) && kill(getpid(), SIGTERM) == 0);
    CHECK(last_signal == SIGTERM);

    double value;
    action.sa_handler = change_fpu;
    CHECK(sigaction(SIGTERM, &action, 0) == 0);
    __asm__ volatile("fld1" ::: "st");
    CHECK(kill(getpid(), SIGTERM) == 0);
    __asm__ volatile("fstpl %0" : "=m"(value) :: "st");
    CHECK(value == 1.0);

    action.sa_handler = fork_in_handler;
    CHECK(sigaction(SIGTERM, &action, 0) == 0 && kill(getpid(), SIGTERM) == 0);
    if (!handler_child) exit(23);
    int status;
    CHECK(handler_child > 0 && waitpid(handler_child, &status, 0) == handler_child && status == (23 << 8));
}

static void dispositions(void) {
    struct sigaction action = {record_signal, 0, 0};
    int ends[2];
    CHECK(sigaction(SIGPIPE, &action, 0) == 0 && pipe(ends) == 0 && close(ends[0]) == 0);
    CHECK(write(ends[1], "x", 1) == -32 && last_signal == SIGPIPE);
    action.sa_handler = SIG_IGN;
    CHECK(sigaction(SIGPIPE, &action, 0) == 0);
    last_signal = 0;
    CHECK(write(ends[1], "x", 1) == -32 && last_signal == 0 && close(ends[1]) == 0);

    action.sa_handler = record_signal;
    CHECK(sigaction(SIGCHLD, &action, 0) == 0);
    int child = fork();
    CHECK(child >= 0);
    if (!child) exit(17);
    int status;
    CHECK(waitpid(child, &status, 0) == child && status == (17 << 8) && last_signal == SIGCHLD);
    action.sa_handler = SIG_IGN;
    CHECK(sigaction(SIGCHLD, &action, 0) == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child) exit(17);
    CHECK(waitpid(child, &status, 0) == -10);
    action.sa_handler = SIG_DFL;
    CHECK(sigaction(SIGCHLD, &action, 0) == 0);
}

static void running_signal(void) {
    struct sigaction action = {record_signal, 0, 0};
    CHECK(sigaction(SIGINT, &action, 0) == 0 && set_foreground(getpid()) == 0);
    last_signal = 0;
    puts("running signal ready\n");
    while (!last_signal) {}
    CHECK(last_signal == SIGINT && set_foreground(0) == 0);
}

enum operation { PIPE_READ, PIPE_WRITE, CHILD_WAIT };

static void blocked_call(enum operation op, unsigned int flags) {
    struct sigaction action = {record_signal, 0, flags};
    CHECK(sigaction(SIGTERM, &action, 0) == 0);
    last_signal = 0;
    int ends[2];
    if (op != CHILD_WAIT) CHECK(pipe(ends) == 0);
    if (op == PIPE_WRITE) {
        static char data[4096];
        CHECK(write(ends[1], data, sizeof(data)) == sizeof(data));
    }
    int child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (op == PIPE_READ) CHECK(close(ends[0]) == 0);
        if (op == PIPE_WRITE) CHECK(close(ends[1]) == 0);
        char byte;
        CHECK(read(0, &byte, 1) == 1 && kill(getppid(), SIGTERM) == 0);
        if (op == CHILD_WAIT) __asm__ volatile("ud2");
        if (op == PIPE_READ) CHECK(write(ends[1], "x", 1) == 1);
        if (op == PIPE_WRITE) {
            char data[128];
            int total = 0, count;
            while ((count = read(ends[0], data, sizeof(data))) > 0) {
                total += count;
                byte = data[count - 1];
            }
            CHECK(count == 0 && total == 4097 && byte == 'x');
        }
        exit(7);
    }
    if (op == PIPE_READ) CHECK(close(ends[1]) == 0);
    if (op == PIPE_WRITE) CHECK(close(ends[0]) == 0);
    puts("blocked signal ready\n");
    char byte = 0;
    int status, result, interrupted = 0;
    for (;;) {
        if (op == PIPE_READ) result = read(ends[0], &byte, 1);
        else if (op == PIPE_WRITE) result = write(ends[1], "x", 1);
        else result = waitpid(child, &status, 0);
        if (result != -4) break;
        CHECK(!flags && !interrupted && last_signal == SIGTERM);
        interrupted = 1;
    }
    CHECK(interrupted == !flags && last_signal == SIGTERM);
    if (op == PIPE_READ) CHECK(result == 1 && byte == 'x' && close(ends[0]) == 0);
    if (op == PIPE_WRITE) CHECK(result == 1 && close(ends[1]) == 0);
    if (op != CHILD_WAIT) result = waitpid(child, &status, 0);
    CHECK(result == child && status == (op == CHILD_WAIT ? 6 : 7 << 8));
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc == 2) {
        struct sigaction action;
        CHECK(sigaction(SIGTERM, 0, &action) == 0 && action.sa_handler == SIG_DFL);
        CHECK(sigaction(SIGPIPE, 0, &action) == 0 && action.sa_handler == SIG_IGN);
        puts("signal handlers and syscall restart verified\n");
        return 0;
    }
    handlers();
    dispositions();
    running_signal();
    for (enum operation op = PIPE_READ; op <= CHILD_WAIT; ++op) {
        blocked_call(op, 0);
        blocked_call(op, SA_RESTART);
    }
    const char *const args[] = {"init", "check-signals", 0};
    execve("/bin/init", args, 0);
    fail("execve");
    return 1;
}
