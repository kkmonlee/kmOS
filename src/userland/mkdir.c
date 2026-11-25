#include "file_command.h"

static int ensure_directory(const char *path) {
    int result = mkdir(path, 0755);
    if (result != -17) return result;
    struct stat info;
    result = stat(path, &info);
    if (result) return result;
    return S_ISDIR(info.st_mode) ? 0 : -20;
}

static int create_parents(char *path) {
    for (char *separator = path; *separator; ++separator) {
        if (*separator != '/' || separator == path || separator[-1] == '/') continue;
        *separator = '\0';
        int result = ensure_directory(path);
        *separator = '/';
        if (result) return result;
    }
    return ensure_directory(path);
}

int main(int argc, char **argv) {
    int parents = 0;
    int first = file_operands(argc, argv, 'p', &parents);
    if (first < 0 || first >= argc) {
        write_text(2, "usage: mkdir [-p] [--] directory ...\n");
        return 2;
    }

    int status = 0;
    for (int i = first; i < argc; ++i) {
        int result = parents ? create_parents(argv[i]) : mkdir(argv[i], 0755);
        if (result) status = file_error("mkdir", argv[i], result);
    }
    return status;
}
