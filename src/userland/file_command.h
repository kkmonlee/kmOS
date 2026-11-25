#ifndef FILE_COMMAND_H
#define FILE_COMMAND_H

#include "user.h"

static inline int file_operands(int argc, char **argv, char option, int *enabled) {
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1]) {
        const char *flags = argv[first++] + 1;
        if (flags[0] == '-' && !flags[1]) break;
        for (; *flags; ++flags) {
            if (!enabled || *flags != option) return -1;
            *enabled = 1;
        }
    }
    return first;
}

static inline int file_message(const char *command, const char *path, const char *message) {
    write_text(2, command);
    write_text(2, ": ");
    write_text(2, path);
    write_text(2, ": ");
    write_text(2, message);
    write_text(2, "\n");
    return 1;
}

static inline int file_error(const char *command, const char *path, int error) {
    const char *message;
    switch (error) {
    case -2: message = "no such file or directory"; break;
    case -5: message = "I/O error"; break;
    case -13: message = "permission denied"; break;
    case -16: message = "device or resource busy"; break;
    case -17: message = "already exists"; break;
    case -18: message = "cross-filesystem move"; break;
    case -20: message = "not a directory"; break;
    case -21: message = "is a directory"; break;
    case -22: message = "invalid path"; break;
    case -27: message = "file too large"; break;
    case -28: message = "no space left"; break;
    case -30: message = "read-only filesystem"; break;
    case -36: message = "path too long"; break;
    case -39: message = "directory not empty"; break;
    default: message = "operation failed"; break;
    }
    return file_message(command, path, message);
}

static inline int file_target(const char *directory, const char *source, char *path, u32 capacity) {
    const char *end = source;
    while (*end) ++end;
    while (end != source && end[-1] == '/') --end;
    const char *name = end;
    while (name != source && name[-1] != '/') --name;
    if (name == end) return -22;

    u32 length = 0;
    while (directory[length]) {
        if (length == capacity - 1) return -36;
        path[length] = directory[length];
        ++length;
    }
    if (length && path[length - 1] != '/') path[length++] = '/';
    if ((u32)(end - name) >= capacity - length) return -36;
    while (name != end) path[length++] = *name++;
    path[length] = '\0';
    return 0;
}

static inline int transfer_files(int argc, char **argv, const char *command,
                                 int (*transfer)(const char *, const char *)) {
    int first = file_operands(argc, argv, 0, 0);
    if (first < 0 || argc - first < 2) {
        write_text(2, "usage: ");
        write_text(2, command);
        write_text(2, " [--] source ... destination\n");
        return 2;
    }
    const char *destination = argv[argc - 1];
    struct stat info;
    int result = stat(destination, &info);
    if (result && result != -2) return file_error(command, destination, result);
    int directory = !result && S_ISDIR(info.st_mode);
    if (argc - first > 2 && !directory) return file_error(command, destination, -20);

    int status = 0;
    for (int i = first; i < argc - 1; ++i) {
        char path[4096];
        const char *target = destination;
        if (directory) {
            result = file_target(destination, argv[i], path, sizeof(path));
            if (result) {
                status = file_error(command, argv[i], result);
                continue;
            }
            target = path;
        }
        if (transfer(argv[i], target)) status = 1;
    }
    return status;
}

#endif
