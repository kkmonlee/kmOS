#include "user.h"

static int transfers(void) {
    int ends[2];
    if (pipe(ends)) return 1;
    int child = fork();
    if (child < 0) return 2;
    if (!child) {
        close(ends[0]);
        if (dup2(ends[1], 31) != 31 || close(ends[1])) return 3;
        unsigned char data[2048];
        for (u32 i = 0; i < 8; ++i) {
            for (u32 j = 0; j < sizeof(data); ++j) data[j] = (i * sizeof(data) + j) % 251;
            if (write(31, data, sizeof(data)) != sizeof(data)) return 4;
        }
        return close(31) ? 5 : 0;
    }
    close(ends[1]);
    unsigned char data[113];
    u32 total = 0;
    int bytes;
    while ((bytes = read(ends[0], data, sizeof(data))) > 0) {
        for (int i = 0; i < bytes; ++i) if (data[i] != (total + i) % 251) return 6;
        total += bytes;
    }
    int status;
    if (bytes || total != 8 * 2048 || waitpid(child, &status, 0) != child || status)
        return 7;
    return close(ends[0]) ? 8 : 0;
}

static int signals(void) {
    int ends[2];
    if (pipe(ends) || close(ends[0])) return 9;
    int child = fork();
    if (child < 0) return 10;
    if (!child) { write(ends[1], "x", 1); return 11; }
    close(ends[1]);
    int status;
    if (waitpid(child, &status, 0) != child || status != 13) return 12;

    if (pipe(ends)) return 13;
    child = fork();
    if (child < 0) return 14;
    if (!child) {
        close(ends[1]);
        char data;
        read(ends[0], &data, 1);
        return 15;
    }
    if (setpgid(child, child) || kill(child, 0) || kill(-child, 15) ||
        waitpid(child, &status, 0) != child || status != 15 || kill(child, 0) != -3)
        return 16;
    close(ends[0]);
    close(ends[1]);
    return 0;
}

static int job_signals(void) {
    int ends[2];
    if (pipe(ends)) return 19;
    int child = fork();
    if (child < 0) return 20;
    if (!child) {
        char value;
        read(ends[0], &value, 1);
        return 21;
    }
    struct sigaction action = {SIG_IGN, 0, 0};
    int status;
    if (sigaction(SIGSTOP, &action, 0) != -22 || setpgid(child, child) ||
        kill(-child, SIGSTOP) || waitpid(0, &status, WNOHANG) != -10 ||
        waitpid(-child, &status, WNOHANG) || waitpid(-child, &status, WUNTRACED) != child ||
        !WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) return 22;
    if (kill(child, SIGSTOP) || waitpid(child, &status, WUNTRACED | WNOHANG) ||
        kill(child, SIGCONT) || waitpid(-child, &status, WCONTINUED) != child ||
        !WIFCONTINUED(status) || waitpid(child, &status, WCONTINUED | WNOHANG)) return 23;
    if (kill(child, SIGTSTP) || waitpid(-child, &status, WUNTRACED) != child ||
        !WIFSTOPPED(status) || WSTOPSIG(status) != SIGTSTP ||
        kill(child, SIGKILL) || waitpid(child, &status, 0) != child || status != SIGKILL) return 24;
    return close(ends[0]) || close(ends[1]);
}

int main(void) {
    int result = transfers();
    if (result) return result;
    result = signals();
    if (result) return result;
    result = job_signals();
    if (result) return result;
    for (int i = 3; i < 31; ++i) if (open("/etc/motd", 0) != i) return 17;
    int ends[2] = {-1, -1};
    if (pipe(ends) != -24 || ends[0] != -1 || ends[1] != -1 || open("/etc/motd", 0) != 31)
        return 18;
    puts("pipe and signal semantics verified\n");
    return 0;
}
