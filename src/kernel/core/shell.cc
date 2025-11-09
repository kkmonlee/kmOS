#include <os.h>
#include <shell.h>
#include <filesystem.h>
#include <arch/x86/keyboard.h>
#include <arch/x86/pit.h>
#include <runtime/alloc.h>

extern "C" {
    int strlen(const char *s);
    int strcmp(const char *dst, const char *src);
    int strncmp(const char *s1, const char *s2, int n);
    int strcpy(char *dst, const char *src);
    int strncpy(char *dst, const char *src, int n);
    char* strcat(char *dst, const char *src);
    char* strchr(const char *s, int c);
    char* strrchr(const char *s, int c);
    void memset(void *ptr, int value, int num);
    void memcpy(void *dst, const void *src, int n);
}

extern IO io;
extern Keyboard keyboard;

// Serial output helper
static void serial_print_shell(const char* str) {
    while (*str) {
        asm volatile("outb %0, %1" : : "a"(*str), "Nd"((unsigned short)0x3F8));
        str++;
    }
}

Shell shell;

// Built-in command table
static struct shell_command builtin_commands[] = {
    {"help",    "Show this help message",                 nullptr},
    {"echo",    "Display a line of text",                 nullptr},
    {"clear",   "Clear the screen",                       nullptr},
    {"ls",      "List directory contents",                nullptr},
    {"cd",      "Change directory",                       nullptr},
    {"pwd",     "Print working directory",                nullptr},
    {"cat",     "Display file contents",                  nullptr},
    {"mkdir",   "Create directory",                       nullptr},
    {"rm",      "Remove files or directories",            nullptr},
    {"cp",      "Copy files",                            nullptr},
    {"mv",      "Move/rename files",                     nullptr},
    {"ps",      "Show running processes",                 nullptr},
    {"mem",     "Show memory information",                nullptr},
    {"swap",    "Show swap information",                  nullptr},
    {"uptime",  "Show system uptime",                     nullptr},
    {"uname",   "Show system information",                nullptr},
    {"exit",    "Exit the shell",                         nullptr}
};

void Shell::init() {
    serial_print_shell("[SHELL] Entering init()\n");
    io.print("[SHELL] Initializing shell\n");
    
    serial_print_shell("[SHELL] About to strcpy\n");
    // Initialize state
    strcpy(state.current_directory, "/");
    serial_print_shell("[SHELL] strcpy done\n");
    memset(state.command_buffer, 0, MAX_COMMAND_LENGTH);
    serial_print_shell("[SHELL] memset done\n");
    state.history_count = 0;
    state.history_index = 0;
    state.running = false;
    state.exit_code = 0;
    
    serial_print_shell("[SHELL] About to init history\n");
    // Initialize command history
    for (int i = 0; i < MAX_HISTORY; i++) {
        state.command_history[i] = nullptr;
    }
    
    serial_print_shell("[SHELL] About to init_commands\n");
    init_commands();
    serial_print_shell("[SHELL] init_commands done\n");
    
    io.print("[SHELL] Shell initialized\n");
    serial_print_shell("[SHELL] Exiting init()\n");
}

void Shell::init_commands() {
    command_count = sizeof(builtin_commands) / sizeof(struct shell_command);
    commands = builtin_commands;
    
    // Set up function pointers
    commands[0].handler = [](int argc, char** argv) { return shell.cmd_help(argc, argv); };
    commands[1].handler = [](int argc, char** argv) { return shell.cmd_echo(argc, argv); };
    commands[2].handler = [](int argc, char** argv) { return shell.cmd_clear(argc, argv); };
    commands[3].handler = [](int argc, char** argv) { return shell.cmd_ls(argc, argv); };
    commands[4].handler = [](int argc, char** argv) { return shell.cmd_cd(argc, argv); };
    commands[5].handler = [](int argc, char** argv) { return shell.cmd_pwd(argc, argv); };
    commands[6].handler = [](int argc, char** argv) { return shell.cmd_cat(argc, argv); };
    commands[7].handler = [](int argc, char** argv) { return shell.cmd_mkdir(argc, argv); };
    commands[8].handler = [](int argc, char** argv) { return shell.cmd_rm(argc, argv); };
    commands[9].handler = [](int argc, char** argv) { return shell.cmd_cp(argc, argv); };
    commands[10].handler = [](int argc, char** argv) { return shell.cmd_mv(argc, argv); };
    commands[11].handler = [](int argc, char** argv) { return shell.cmd_ps(argc, argv); };
    commands[12].handler = [](int argc, char** argv) { return shell.cmd_mem(argc, argv); };
    commands[13].handler = [](int argc, char** argv) { return shell.cmd_swap(argc, argv); };
    commands[14].handler = [](int argc, char** argv) { return shell.cmd_uptime(argc, argv); };
    commands[15].handler = [](int argc, char** argv) { return shell.cmd_uname(argc, argv); };
    commands[16].handler = [](int argc, char** argv) { return shell.cmd_exit(argc, argv); };
}

void Shell::run() {
    serial_print_shell("[SHELL] run() called\n");
    io.print("[SHELL] About to print banner\n");
    serial_print_shell("[SHELL] io.print returned from banner message\n");
    print_banner();
    serial_print_shell("[SHELL] print_banner() returned\n");
    io.print("[SHELL] Banner printed, entering main loop\n");
    state.running = true;
    
    while (state.running) {
        serial_print_shell("[SHELL] Loop iteration start\n");
        io.print("[SHELL] About to print prompt\n");
        print_prompt();
        io.print("[SHELL] Prompt printed, waiting for input\n");
        
        // Read command line
        int len = keyboard.read_line(state.command_buffer, MAX_COMMAND_LENGTH);
        if (len > 0) {
            // Parse and execute command
            char* args[MAX_ARGS];
            int argc = parse_command(state.command_buffer, args);
            
            if (argc > 0) {
                add_to_history(state.command_buffer);
                execute_command(argc, args);
            }
        }
    }
    
    io.print("Shell exiting with code %d\n", state.exit_code);
}

void Shell::print_banner() {
    clear_screen();
    io.print("╔════════════════════════════════════════════════════════════════════════════════╗\n");
    io.print("║                                    kmOS Shell                                  ║\n");
    io.print("║                              Welcome to kmOS v1.0                              ║\n");
    io.print("╚════════════════════════════════════════════════════════════════════════════════╝\n");
    io.print("\n");
    io.print("Type 'help' for a list of available commands.\n\n");
}

void Shell::print_prompt() {
    io.print("kmOS:%s$ ", state.current_directory);
}

int Shell::parse_command(const char* input, char** args) {
    static char buffer[MAX_COMMAND_LENGTH];
    strcpy(buffer, input);
    
    int argc = 0;
    char* token = buffer;
    bool in_quotes = false;
    
    // Simple tokenization
    for (int i = 0; buffer[i] && argc < MAX_ARGS - 1; i++) {
        if (buffer[i] == '"') {
            in_quotes = !in_quotes;
            // Remove quotes
            for (int j = i; buffer[j]; j++) {
                buffer[j] = buffer[j + 1];
            }
            i--;
        } else if (!in_quotes && (buffer[i] == ' ' || buffer[i] == '\t')) {
            buffer[i] = '\0';
            if (strlen(token) > 0) {
                args[argc++] = token;
            }
            // Skip whitespace
            while (buffer[i + 1] && (buffer[i + 1] == ' ' || buffer[i + 1] == '\t')) {
                i++;
            }
            token = &buffer[i + 1];
        }
    }
    
    // Add last token
    if (strlen(token) > 0) {
        args[argc++] = token;
    }
    
    args[argc] = nullptr;
    return argc;
}

int Shell::execute_command(int argc, char** args) {
    if (argc == 0) return 0;
    
    struct shell_command* cmd = find_command(args[0]);
    if (cmd && cmd->handler) {
        return cmd->handler(argc, args);
    } else {
        io.print("Command not found: %s\n", args[0]);
        io.print("Type 'help' for a list of available commands.\n");
        return 1;
    }
}

struct shell_command* Shell::find_command(const char* name) {
    for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return nullptr;
}

// Built-in command implementations
int Shell::cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    
    io.print("Available commands:\n\n");
    for (int i = 0; i < command_count; i++) {
        io.print("  %-10s - %s\n", commands[i].name, commands[i].description);
    }
    io.print("\n");
    return 0;
}

int Shell::cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        io.print("%s", argv[i]);
        if (i < argc - 1) io.print(" ");
    }
    io.print("\n");
    return 0;
}

int Shell::cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    clear_screen();
    return 0;
}

int Shell::cmd_ls(int argc, char** argv) {
    (void)argc; (void)argv;
    
    const char* path = (argc > 1) ? argv[1] : state.current_directory;
    File* dir = fsm.path(path);
    
    if (!dir) {
        io.print("ls: cannot access '%s': No such file or directory\n", path);
        return 1;
    }
    
    io.print("Contents of %s:\n", path);
    io.print("  .   (current directory)\n");
    io.print("  ..  (parent directory)\n");
    
    // TODO: Implement actual directory listing when filesystem is complete
    io.print("  (filesystem directory listing not yet implemented)\n");
    
    return 0;
}

int Shell::cmd_cd(int argc, char** argv) {
    if (argc < 2) {
        strcpy(state.current_directory, "/");
        return 0;
    }
    
    const char* new_dir = argv[1];
    
    if (strcmp(new_dir, "..") == 0) {
        // Go to parent directory
        char* last_slash = strrchr(state.current_directory, '/');
        if (last_slash && last_slash != state.current_directory) {
            *last_slash = '\0';
        } else {
            strcpy(state.current_directory, "/");
        }
    } else if (strcmp(new_dir, ".") == 0) {
        // Stay in current directory
        return 0;
    } else {
        // Change to specified directory
        char* full_path = get_full_path(new_dir);
        if (full_path) {
            File* dir = fsm.path(full_path);
            if (dir) {
                strcpy(state.current_directory, full_path);
            } else {
                io.print("cd: %s: No such file or directory\n", new_dir);
                kfree(full_path);
                return 1;
            }
            kfree(full_path);
        }
    }
    
    return 0;
}

int Shell::cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("%s\n", state.current_directory);
    return 0;
}

int Shell::cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        io.print("Usage: cat <filename>\n");
        return 1;
    }
    
    char* full_path = get_full_path(argv[1]);
    if (!full_path) {
        io.print("cat: memory allocation failed\n");
        return 1;
    }
    
    File* file = fsm.path(full_path);
    kfree(full_path);
    
    if (!file) {
        io.print("cat: %s: No such file or directory\n", argv[1]);
        return 1;
    }
    
    // TODO: Implement actual file reading when filesystem is complete
    io.print("(file content display not yet implemented)\n");
    return 0;
}

int Shell::cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("PID  PPID CMD\n");
    io.print("  1     0 kernel\n");
    io.print("  2     1 shell\n");
    return 0;
}

int Shell::cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    // TODO: Get actual memory statistics
    io.print("Memory Information:\n");
    io.print("  Total Memory:    64 MB\n");
    io.print("  Used Memory:     12 MB\n");
    io.print("  Free Memory:     52 MB\n");
    io.print("  Kernel Memory:    8 MB\n");
    io.print("  User Memory:      4 MB\n");
    return 0;
}

int Shell::cmd_swap(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("Swap Information:\n");
    io.print("  Total Swap:       0 MB\n");
    io.print("  Used Swap:        0 MB\n");
    io.print("  Free Swap:        0 MB\n");
    io.print("  Swap Devices:     0\n");
    return 0;
}

int Shell::cmd_uptime(int argc, char** argv) {
    (void)argc; (void)argv;
    
    u64 uptime_ms = pit.get_uptime_ms();
    u64 seconds = uptime_ms / 1000;
    u64 minutes = seconds / 60;
    u64 hours = minutes / 60;
    u64 days = hours / 24;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    io.print("System uptime: %d days, %d hours, %d minutes, %d seconds\n",
             (u32)days, (u32)hours, (u32)minutes, (u32)seconds);
    io.print("Timer ticks: %d\n", (u32)pit.get_ticks());
    
    return 0;
}

int Shell::cmd_uname(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("kmOS 1.0 x86 kernel\n");
    io.print("Created by: @kkmonlee\n");
    return 0;
}

int Shell::cmd_exit(int argc, char** argv) {
    int code = 0;
    if (argc > 1) {
        // Simple string to int conversion
        code = 0;
        for (int i = 0; argv[1][i]; i++) {
            if (argv[1][i] >= '0' && argv[1][i] <= '9') {
                code = code * 10 + (argv[1][i] - '0');
            }
        }
    }
    exit(code);
    return code;
}

// Utility command stubs
int Shell::cmd_mkdir(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("mkdir: command not yet implemented\n");
    return 1;
}

int Shell::cmd_rm(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("rm: command not yet implemented\n");
    return 1;
}

int Shell::cmd_cp(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("cp: command not yet implemented\n");
    return 1;
}

int Shell::cmd_mv(int argc, char** argv) {
    (void)argc; (void)argv;
    io.print("mv: command not yet implemented\n");
    return 1;
}

// Utility functions
void Shell::clear_screen() {
    // VGA text mode: clear screen by writing spaces
    volatile unsigned short *video_memory = (volatile unsigned short*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = 0x0720; // Space character with light gray on black
    }
}

void Shell::add_to_history(const char* command) {
    if (state.history_count < MAX_HISTORY) {
        state.command_history[state.history_count] = (char*)kmalloc(strlen(command) + 1);
        if (state.command_history[state.history_count]) {
            strcpy(state.command_history[state.history_count], command);
            state.history_count++;
        }
    } else {
        // Shift history and add new command
        kfree(state.command_history[0]);
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            state.command_history[i] = state.command_history[i + 1];
        }
        state.command_history[MAX_HISTORY - 1] = (char*)kmalloc(strlen(command) + 1);
        if (state.command_history[MAX_HISTORY - 1]) {
            strcpy(state.command_history[MAX_HISTORY - 1], command);
        }
    }
}

char* Shell::get_full_path(const char* path) {
    if (is_absolute_path(path)) {
        char* result = (char*)kmalloc(strlen(path) + 1);
        if (result) {
            strcpy(result, path);
        }
        return result;
    } else {
        return join_path(state.current_directory, path);
    }
}

bool Shell::is_absolute_path(const char* path) {
    return path && path[0] == '/';
}

char* Shell::join_path(const char* dir, const char* file) {
    int dir_len = strlen(dir);
    int file_len = strlen(file);
    int total_len = dir_len + file_len + 2; // +1 for '/', +1 for '\0'
    
    char* result = (char*)kmalloc(total_len);
    if (!result) return nullptr;
    
    strcpy(result, dir);
    if (dir_len > 0 && dir[dir_len - 1] != '/') {
        strcat(result, "/");
    }
    strcat(result, file);
    
    return result;
}

void Shell::exit(int code) {
    state.running = false;
    state.exit_code = code;
    io.print("Goodbye!\n");
}

// C interface functions
extern "C" {
    void init_shell() {
        shell.init();
    }
    
    void run_shell() {
        shell.run();
    }
    
    void shell_exit(int code) {
        shell.exit(code);
    }
}