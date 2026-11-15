#include "user.h"

enum { max_commands = 8, max_args = 32, line_size = 512, max_jobs = 16 };

typedef struct {
    int argc;
    char *argv[max_args + 1];
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

static int parse(char *line, Command *commands, int *background) {
    char *out = line;
    int index = 0;
    for (;;) {
        while (*line == ' ' || *line == '\t') ++line;
        if (!*line) break;
        Command *command = &commands[index];
        if (*line == '|' || *line == '&' || command->argc == max_args) return -1;
        command->argv[command->argc++] = out;
        char quote = 0;
        while (*line) {
            char c = *line;
            if (!quote && (c == ' ' || c == '\t' || c == '|' || c == '&')) break;
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
        if (!delimiter) break;
        while (*line == ' ' || *line == '\t') ++line;
        if ((delimiter == ' ' || delimiter == '\t') && (*line == '|' || *line == '&')) {
            delimiter = *line++;
        }
        if (delimiter == '|') {
            if (++index == max_commands) return -1;
        } else if (delimiter == '&') {
            while (*line == ' ' || *line == '\t') ++line;
            if (*line) return -1;
            *background = 1;
            break;
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
    if (!failed && terminal && !background && set_foreground(job->group) < 0) failed = 1;
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
    if (terminal) set_foreground(0);
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
    if (!job || (foreground && set_foreground(job->group) < 0)) {
        error("sh: no such job\n");
        return;
    }
    if (job->stopped) {
        if (kill(-job->group, SIGCONT) < 0) {
            if (foreground) set_foreground(0);
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
        for (int i = 0; (arguments[i] = line[i]); ++i) {}
        Command commands[max_commands] = {0};
        int background = 0;
        int count = parse(arguments, commands, &background);
        if (count < 0) { error("sh: syntax error\n"); continue; }
        if (!count) continue;
        Command *command = &commands[0];
        const char *name = command->argv[0];
        if (background) {
            if (job_control) run_job(commands, count, line, 1, 1);
            else error("sh: job control unavailable\n");
        } else if (count == 1 && equal(name, "exit")) {
            break;
        } else if (count == 1 && equal(name, "cd")) {
            if (command->argc > 2 || chdir(command->argc == 2 ? command->argv[1] : "/"))
                error("sh: cd failed\n");
        } else if (count == 1 && equal(name, "kill")) {
            int pid = command->argc == 2 ? decimal(command->argv[1]) : -1;
            if (pid <= 0 || kill(pid, 15)) error("sh: kill failed\n");
        } else if (count == 1 && equal(name, "help")) {
            puts("cd [dir], echo [text], kill pid, jobs, fg [%job], bg [%job], exit\n");
        } else if (count == 1 && equal(name, "jobs")) {
            reap_jobs();
            list_jobs();
        } else if (count == 1 && (equal(name, "fg") || equal(name, "bg"))) {
            reap_jobs();
            resume_job(command, equal(name, "fg"));
        } else {
            run_job(commands, count, line, terminal, 0);
        }
    }
    close_jobs();
    return 0;
}
