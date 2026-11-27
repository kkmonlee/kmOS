#include "user.h"

enum { max_commands = 8, max_args = 32, max_redirects = 8, line_size = 512, max_jobs = 16 };

typedef struct {
    int fd;
    int flags;
    const char *path;
} Redirect;

typedef struct {
    int argc;
    char *argv[max_args + 1];
    int redirect_count;
    Redirect redirects[max_redirects];
} Command;

typedef struct {
    int group;
    int count;
    int pids[max_commands];
    u32 stopped;
    u32 order;
    char line[line_size];
} Job;

static Job jobs[max_jobs];
static u32 job_order;
static volatile int reading_command;
static volatile int child_changed;
static int terminal_fd;

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

static void print_number(unsigned int value) {
    char digits[10];
    int count = 0;
    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (count) write_all(1, &digits[--count], 1);
}

enum job_state { JOB_DONE, JOB_RUNNING, JOB_STOPPED };

static enum job_state job_state(const Job *job) {
    int stopped = 0;
    for (int i = 0; i < job->count; ++i) {
        if (!job->pids[i]) continue;
        if (!(job->stopped & (1U << i))) return JOB_RUNNING;
        stopped = 1;
    }
    return stopped ? JOB_STOPPED : JOB_DONE;
}

static void record_wait(Job *job, int index, int status) {
    u32 bit = 1U << index;
    if (WIFSTOPPED(status)) {
        job->stopped |= bit;
    } else {
        job->stopped &= ~bit;
        if (!WIFCONTINUED(status)) job->pids[index] = 0;
    }
}

static void reap_jobs(void) {
    for (int i = 0; i < max_jobs; ++i) {
        Job *job = &jobs[i];
        if (!job->group) continue;
        for (int j = 0; j < job->count; ++j) {
            if (!job->pids[j]) continue;
            int status = 0;
            int result = waitpid(job->pids[j], &status, WNOHANG | WUNTRACED | WCONTINUED);
            if (result == job->pids[j] || result == -10) record_wait(job, j, status);
        }
        if (job_state(job) == JOB_DONE) job->group = 0;
    }
}

static void child_exit(int signal) {
    (void)signal;
    if (reading_command) reap_jobs();
    else child_changed = 1;
}

static int input(char *line) {
    u32 used = 0;
    int too_long = 0;
    for (;;) {
        char c;
        int count = read(0, &c, 1);
        if (count <= 0) return -1;
        if (c == 3) { puts("^C\n"); return 0; }
        if (c == 4 && !used) return -1;
        if (c == '\n' || c == '\r') {
            puts("\n");
            if (too_long) {
                error("sh: line too long\n");
                return 0;
            }
            line[used] = 0;
            return 1;
        }
        if (too_long) continue;
        if (c == '\b' || c == 127) {
            if (used) { --used; puts("\b \b"); }
        } else if (c >= ' ' || c == '\t') {
            if (used + 1 == line_size) { too_long = 1; continue; }
            line[used++] = c;
            write_all(1, &c, 1);
        }
    }
}

static const char *skip_space(const char *line) {
    while (*line == ' ' || *line == '\t') ++line;
    return line;
}

static int word_end(char c) {
    return !c || c == ' ' || c == '\t' || c == '|' || c == '&' || c == '<' || c == '>';
}

static char *parse_word(const char **input, char **output) {
    const char *line = *input;
    if (word_end(*line)) return 0;
    char *out = *output;
    char *word = out;
    char quote = 0;
    while (*line && (quote || !word_end(*line))) {
        char c = *line++;
        if (c == quote) quote = 0;
        else if (!quote && (c == '\'' || c == '"')) quote = c;
        else if (c == '\\' && quote != '\'') {
            if (!*line) return 0;
            *out++ = *line++;
        } else {
            *out++ = c;
        }
    }
    if (quote) return 0;
    *out++ = 0;
    *input = line;
    *output = out;
    return word;
}

static int parse(const char *line, char *words, Command *commands, int *background) {
    int index = 0;
    for (line = skip_space(line); *line; line = skip_space(line)) {
        Command *command = &commands[index];
        if (*line == '|' || *line == '&') {
            if (!command->argc && !command->redirect_count) return -1;
            if (*line++ == '&') {
                if (*skip_space(line)) return -1;
                *background = 1;
                break;
            }
            if (++index == max_commands) return -1;
            continue;
        }

        const char *operator = line;
        while (*operator >= '0' && *operator <= '9') ++operator;
        if (*operator == '<' || *operator == '>') {
            if (command->redirect_count == max_redirects) return -1;
            int fd = *operator == '>';
            if (operator != line) {
                if (operator - line != 1 || *line > '2') return -1;
                fd = *line - '0';
            }
            line = operator + 1;
            int flags = O_RDONLY;
            if (*operator == '>') {
                flags = O_WRONLY | O_CREAT | (*line == '>' ? O_APPEND : O_TRUNC);
                if (*line == '>') ++line;
            }
            line = skip_space(line);
            char *path = parse_word(&line, &words);
            if (!path) return -1;
            command->redirects[command->redirect_count++] = (Redirect){fd, flags, path};
            continue;
        }

        if (command->argc == max_args) return -1;
        char *word = parse_word(&line, &words);
        if (!word) return -1;
        command->argv[command->argc++] = word;
    }
    if (!commands[index].argc && !commands[index].redirect_count) return index ? -1 : 0;
    return index + 1;
}

static int redirect(const Command *command) {
    for (int i = 0; i < command->redirect_count; ++i) {
        const Redirect *target = &command->redirects[i];
        int fd = open(target->path, target->flags, 0666);
        if (fd >= 0) {
            if (fd == target->fd) continue;
            int result = dup2(fd, target->fd);
            close(fd);
            if (result >= 0) continue;
        }
        error("sh: cannot redirect: ");
        error(target->path);
        error("\n");
        return -1;
    }
    return 0;
}

static void close_pipes(int pipes[][2], int count) {
    for (int i = 0; i < count; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static void run_command(const Command *command) {
    if (redirect(command)) exit(1);
    if (!command->argc) exit(0);
    const char *name = command->argv[0];
    if (equal(name, "echo")) {
        for (int i = 1; i < command->argc; ++i) {
            if (i > 1 && puts(" ")) exit(1);
            if (puts(command->argv[i])) exit(1);
        }
        exit(puts("\n") != 0);
    }
    if (equal(name, "pwd")) {
        if (command->argc != 1) {
            error("usage: pwd\n");
            exit(2);
        }
        char path[4096];
        int length = getcwd(path, sizeof(path));
        if (length < 0) {
            error("sh: cannot get current directory\n");
            exit(1);
        }
        path[length - 1] = '\n';
        exit(write_all(1, path, length) != 0);
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

static void collect_job(Job *job) {
    for (int i = 0; i < job->count; ++i) {
        int pid = job->pids[i];
        if (!pid) continue;
        while (waitpid(pid, 0, 0) == -4) {}
    }
    job->group = 0;
}

static int start_pipeline(Command *commands, int count, int terminal, int background, Job *job) {
    int pipes[max_commands - 1][2];
    for (int i = 0; i + 1 < count; ++i) {
        if (pipe(pipes[i]) < 0) {
            close_pipes(pipes, i);
            return -1;
        }
    }

    int gate[2];
    if (pipe(gate) < 0) {
        close_pipes(pipes, count - 1);
        return -1;
    }
    job->count = 0;
    job->stopped = 0;
    int failed = 0;
    for (int i = 0; i < count; ++i) {
        int pid = fork();
        if (pid < 0) {
            failed = 1;
            break;
        }
        if (!pid) {
            close(gate[1]);
            if ((i && dup2(pipes[i - 1][0], 0) < 0) ||
                (i + 1 < count && dup2(pipes[i][1], 1) < 0)) exit(1);
            close_pipes(pipes, count - 1);
            char value;
            if (read(gate[0], &value, 1) != 0 || close(gate[0])) exit(1);
            run_command(&commands[i]);
        }
        if (!job->count) job->group = pid;
        job->pids[job->count++] = pid;
        if (terminal && setpgid(pid, job->group) < 0) {
            failed = 1;
            break;
        }
    }

    close_pipes(pipes, count - 1);
    close(gate[0]);
    if (!failed && terminal && !background && set_terminal_group(terminal_fd, job->group) < 0) failed = 1;
    if (failed) {
        for (int i = 0; i < job->count; ++i) kill(job->pids[i], SIGKILL);
    }
    close(gate[1]);
    if (failed) collect_job(job);
    return failed ? -1 : 0;
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

static void print_job_id(int index) {
    puts("[");
    print_number(index + 1);
    puts("] ");
}

static void print_job(Job *job) {
    print_job_id(job - jobs);
    puts(job_state(job) == JOB_STOPPED ? "Stopped  " : "Running  ");
    puts(job->line);
    puts("\n");
}

static void list_jobs(void) {
    for (int i = 0; i < max_jobs; ++i) {
        if (jobs[i].group) print_job(&jobs[i]);
    }
}

static void wait_foreground(Job *job, int terminal) {
    int interrupted = 0;
    int selector = terminal ? -job->group : -1;
    int options = terminal ? WUNTRACED | WCONTINUED : 0;
    while (job_state(job) == JOB_RUNNING) {
        int status, pid = waitpid(selector, &status, options);
        if (pid == -4) continue;
        if (pid < 0) break;
        if (status == SIGINT) interrupted = 1;
        for (int i = 0; i < job->count; ++i) {
            if (job->pids[i] == pid) {
                record_wait(job, i, status);
                break;
            }
        }
    }
    if (terminal) set_terminal_group(terminal_fd, 0);
    if (terminal && interrupted) puts("^C\n");
    if (terminal && job_state(job) == JOB_STOPPED) print_job(job);
    else job->group = 0;
}

static void resume_job(Command *command, int foreground) {
    Job *job = 0;
    if (command->argc == 1) {
        for (int i = 0; i < max_jobs; ++i) {
            if (jobs[i].group && (!job || job_order - jobs[i].order < job_order - job->order)) job = &jobs[i];
        }
    } else if (command->argc == 2) {
        const char *id = command->argv[1];
        int number = decimal(id + (*id == '%'));
        if (number > 0 && number <= max_jobs && jobs[number - 1].group) job = &jobs[number - 1];
    }
    if (!job || (foreground && set_terminal_group(terminal_fd, job->group) < 0)) {
        error("sh: no such job\n");
        return;
    }
    if (job->stopped) {
        if (kill(-job->group, SIGCONT) < 0) {
            if (foreground) set_terminal_group(terminal_fd, 0);
            error("sh: no such job\n");
            return;
        }
        job->stopped = 0;
    }
    job->order = ++job_order;
    if (foreground) wait_foreground(job, 1);
    else print_job(job);
}

static void run_job(Command *commands, int count, const char *line, int terminal, int background) {
    Job local = {0};
    Job *job = &local;
    if (terminal) {
        int index = 0;
        while (index < max_jobs && jobs[index].group) ++index;
        if (index == max_jobs) {
            error("sh: too many jobs\n");
            return;
        }
        job = &jobs[index];
    }
    if (start_pipeline(commands, count, terminal, background, job)) {
        error("sh: cannot start pipeline\n");
        return;
    }
    job->order = ++job_order;
    for (int i = 0; (job->line[i] = line[i]); ++i) {}
    if (background) {
        print_job_id(job - jobs);
        print_number(job->group);
        puts("\n");
    } else {
        wait_foreground(job, terminal);
    }
}

static void close_jobs(void) {
    for (int i = 0; i < max_jobs; ++i)
        if (jobs[i].group) kill(-jobs[i].group, SIGKILL);
    for (int i = 0; i < max_jobs; ++i)
        if (jobs[i].group) collect_job(&jobs[i]);
}

enum builtin { NONE, EXIT, CD, KILL, HELP, JOBS, FG, BG };

static enum builtin find_builtin(const Command *command) {
    static const char *const names[] = {"exit", "cd", "kill", "help", "jobs", "fg", "bg"};
    if (command->argc) {
        for (u32 i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (equal(command->argv[0], names[i])) return (enum builtin)(i + 1);
    }
    return NONE;
}

static int run_builtin(Command *command, enum builtin builtin) {
    int saved[] = {-1, -1, -1};
    int leaving = 0;
    for (int i = 0; i < command->redirect_count; ++i) {
        int fd = command->redirects[i].fd;
        if (saved[fd] != -1) continue;
        saved[fd] = dup(fd);
        if (saved[fd] < 3) {
            if (saved[fd] >= 0) close(saved[fd]);
            saved[fd] = -1;
            error("sh: cannot save descriptor\n");
            goto restore;
        }
    }
    if (saved[0] >= 0) terminal_fd = saved[0];
    if (redirect(command)) goto restore;
    switch (builtin) {
    case EXIT:
        leaving = 1;
        break;
    case CD:
        if (command->argc > 2 || chdir(command->argc == 2 ? command->argv[1] : "/"))
            error("sh: cd failed\n");
        break;
    case KILL: {
        int pid = command->argc == 2 ? decimal(command->argv[1]) : -1;
        if (pid <= 0 || kill(pid, SIGTERM)) error("sh: kill failed\n");
        break;
    }
    case HELP:
        puts("cd [dir], pwd, echo [text], kill pid, jobs, fg [%job], bg [%job], exit\n");
        puts("mkdir [-p] dir..., rmdir dir..., rm [-f] file...\n");
        puts("cp source... destination, mv source... destination\n");
        puts("ln [-s] source... destination, readlink path\n");
        break;
    case JOBS:
        reap_jobs();
        list_jobs();
        break;
    case FG:
    case BG:
        reap_jobs();
        resume_job(command, builtin == FG);
        break;
    default:
        break;
    }

restore:
    terminal_fd = 0;
    for (u32 fd = 0; fd < sizeof(saved) / sizeof(saved[0]); ++fd) {
        if (saved[fd] >= 0) {
            dup2(saved[fd], fd);
            close(saved[fd]);
        }
    }
    return leaving;
}

int main(void) {
    char line[line_size];
    int terminal = set_foreground(0) == 0;
    struct sigaction action = {child_exit, 0, SA_RESTART};
    int job_control = terminal && sigaction(SIGCHLD, &action, 0) == 0;
    for (;;) {
        do {
            reading_command = 0;
            child_changed = 0;
            reap_jobs();
            reading_command = 1;
        } while (child_changed);
        puts("sh$ ");
        int read_line = input(line);
        reading_command = 0;
        if (read_line < 0) { puts("\n"); break; }
        if (!read_line) continue;
        char arguments[line_size];
        Command commands[max_commands] = {0};
        int background = 0;
        int count = parse(line, arguments, commands, &background);
        if (count < 0) { error("sh: syntax error\n"); continue; }
        if (!count) continue;
        enum builtin builtin = count == 1 && !background ? find_builtin(&commands[0]) : NONE;
        if (background) {
            if (job_control) run_job(commands, count, line, 1, 1);
            else error("sh: job control unavailable\n");
        } else if (builtin != NONE) {
            if (run_builtin(&commands[0], builtin)) break;
        } else {
            run_job(commands, count, line, terminal, 0);
        }
    }
    close_jobs();
    return 0;
}
