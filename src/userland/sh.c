#include "user.h"

enum { max_commands = 8, max_args = 32, line_size = 512 };

typedef struct {
    int argc;
    char *argv[max_args + 1];
} Command;

static int equal(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static u32 length(const char *text) {
    u32 size = 0;
    while (text[size]) ++size;
    return size;
}

static void error(const char *text) { write_all(2, text, length(text)); }

static int input(char *line) {
    u32 used = 0;
    for (;;) {
        char c;
        int count = read(0, &c, 1);
        if (count <= 0) return -1;
        if (c == 3) { puts("^C\n"); return 0; }
        if (c == 4 && !used) return -1;
        if (c == '\n' || c == '\r') {
            puts("\n");
            line[used] = 0;
            return 1;
        }
        if (c == '\b' || c == 127) {
            if (used) { --used; puts("\b \b"); }
        } else if ((c >= ' ' || c == '\t') && used + 1 < line_size) {
            line[used++] = c;
            write_all(1, &c, 1);
        }
    }
}

static int parse(char *line, Command *commands) {
    char *out = line;
    int index = 0;
    for (;;) {
        while (*line == ' ' || *line == '\t') ++line;
        if (!*line) break;
        Command *command = &commands[index];
        if (*line == '|' || command->argc == max_args) return -1;
        command->argv[command->argc++] = out;
        char quote = 0;
        while (*line) {
            char c = *line;
            if (!quote && (c == ' ' || c == '\t' || c == '|')) break;
            ++line;
            if (c == quote) quote = 0;
            else if (!quote && (c == '\'' || c == '"')) quote = c;
            else if (c == '\\' && quote != '\'') {
                if (!*line) return -1;
                *out++ = *line++;
            } else {
                *out++ = c;
            }
        }
        if (quote) return -1;
        char delimiter = *line;
        if (delimiter) ++line;
        *out++ = 0;
        command->argv[command->argc] = 0;
        if (delimiter == '|') {
            if (++index == max_commands) return -1;
        }
        if (!delimiter) break;
        while (*line == ' ' || *line == '\t') ++line;
        if (delimiter != '|' && *line == '|') {
            ++line;
            if (++index == max_commands) return -1;
        }
    }
    if (!commands[index].argc) return index ? -1 : 0;
    return index + 1;
}

static void close_pipes(int pipes[][2], int count) {
    for (int i = 0; i < count; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static void run_command(const Command *command) {
    const char *name = command->argv[0];
    if (equal(name, "echo")) {
        for (int i = 1; i < command->argc; ++i) {
            if (i > 1 && puts(" ")) exit(1);
            if (puts(command->argv[i])) exit(1);
        }
        exit(puts("\n") != 0);
    }

    for (const char *c = name; *c; ++c) {
        if (*c == '/') {
            execve(name, (const char *const *)command->argv, 0);
            error("sh: cannot run command\n");
            exit(126);
        }
    }
    char path[256] = "/bin/";
    u32 size = length(name);
    if (size < sizeof(path) - 5) {
        for (u32 i = 0; i <= size; ++i) path[5 + i] = name[i];
        execve(path, (const char *const *)command->argv, 0);
    }
    error("sh: cannot run command\n");
    exit(127);
}

static void run_pipeline(Command *commands, int count, int foreground) {
    int pipes[max_commands - 1][2];
    for (int i = 0; i + 1 < count; ++i) {
        if (pipe(pipes[i]) < 0) {
            close_pipes(pipes, i);
            error("sh: pipe failed\n");
            return;
        }
    }

    int group = 0, started = 0, pids[max_commands];
    for (int i = 0; i < count; ++i) {
        int pid = fork();
        if (pid < 0) {
            error("sh: fork failed\n");
            break;
        }
        if (!pid) {
            if ((foreground && setpgid(0, group) < 0) ||
                (i && dup2(pipes[i - 1][0], 0) < 0) ||
                (i + 1 < count && dup2(pipes[i][1], 1) < 0)) exit(1);
            close_pipes(pipes, count - 1);
            run_command(&commands[i]);
        }
        pids[started++] = pid;
        if (foreground) {
            if (!group) group = pid;
            setpgid(pid, group);
        }
    }

    close_pipes(pipes, count - 1);
    if (started < count) for (int i = 0; i < started; ++i) kill(pids[i], 15);
    if (group) set_foreground(group);
    int interrupted = 0;
    for (int i = 0; i < started; ++i) {
        int status;
        if (waitpid(pids[i], &status, 0) == pids[i] && status == 2) interrupted = 1;
    }
    if (group) set_foreground(0);
    if (interrupted) puts("^C\n");
}

static int decimal(const char *text) {
    unsigned int value = 0;
    if (!*text) return -1;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || value > (0x7fffffffU - (*text - '0')) / 10)
            return -1;
        value = value * 10 + (*text - '0');
    }
    return value;
}

int main(void) {
    char line[line_size];
    int foreground = set_foreground(0) == 0;
    for (;;) {
        puts("sh$ ");
        int read_line = input(line);
        if (read_line < 0) { puts("\n"); return 0; }
        if (!read_line) continue;
        Command commands[max_commands] = {0};
        int count = parse(line, commands);
        if (count < 0) { error("sh: syntax error\n"); continue; }
        if (!count) continue;
        Command *command = &commands[0];
        const char *name = command->argv[0];
        if (count == 1 && equal(name, "exit")) return 0;
        if (count == 1 && equal(name, "cd")) {
            if (command->argc > 2 || chdir(command->argc == 2 ? command->argv[1] : "/"))
                error("sh: cd failed\n");
        } else if (count == 1 && equal(name, "kill")) {
            int pid = command->argc == 2 ? decimal(command->argv[1]) : -1;
            if (pid <= 0 || kill(pid, 15)) error("sh: kill failed\n");
        } else if (count == 1 && equal(name, "help")) {
            puts("cd [dir], echo [text], kill pid, exit\n");
        } else {
            run_pipeline(commands, count, foreground);
        }
    }
}
