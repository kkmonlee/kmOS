#ifndef SHELL_H
#define SHELL_H

#include <runtime/types.h>
#include <core/file.h>

#define MAX_COMMAND_LENGTH 512
#define MAX_ARGS 32
#define MAX_PATH_LENGTH 256
#define MAX_HISTORY 10

struct shell_command {
    const char* name;
    const char* description;
    int (*handler)(int argc, char** argv);
};

struct shell_state {
    char current_directory[MAX_PATH_LENGTH];
    char command_buffer[MAX_COMMAND_LENGTH];
    char* command_history[MAX_HISTORY];
    int history_count;
    int history_index;
    bool running;
    int exit_code;
};

class Shell {
public:
    void init();
    void run();
    void exit(int code);
    
    // Command parsing
    int parse_command(const char* input, char** args);
    int execute_command(int argc, char** args);
    
    // Built-in commands
    int cmd_help(int argc, char** argv);
    int cmd_echo(int argc, char** argv);
    int cmd_clear(int argc, char** argv);
    int cmd_ls(int argc, char** argv);
    int cmd_cd(int argc, char** argv);
    int cmd_pwd(int argc, char** argv);
    int cmd_cat(int argc, char** argv);
    int cmd_mkdir(int argc, char** argv);
    int cmd_rm(int argc, char** argv);
    int cmd_cp(int argc, char** argv);
    int cmd_mv(int argc, char** argv);
    int cmd_ps(int argc, char** argv);
    int cmd_mem(int argc, char** argv);
    int cmd_swap(int argc, char** argv);
    int cmd_uptime(int argc, char** argv);
    int cmd_uname(int argc, char** argv);
    int cmd_exit(int argc, char** argv);
    
    // Utility functions
    void print_prompt();
    void print_banner();
    void add_to_history(const char* command);
    void clear_screen();
    void print_file_info(File* file);
    char* get_full_path(const char* path);
    
private:
    struct shell_state state;
    struct shell_command* commands;
    int command_count;
    
    void init_commands();
    struct shell_command* find_command(const char* name);
    void trim_whitespace(char* str);
    bool is_absolute_path(const char* path);
    char* join_path(const char* dir, const char* file);
};

extern Shell shell;

// C interface
extern "C" {
    void init_shell();
    void run_shell();
    void shell_exit(int code);
}

#endif