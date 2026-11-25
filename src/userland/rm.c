#include "file_command.h"

static int missing(const char *path, int result) {
    if (result == -2) return 1;
    if (result != -30) return 0;
    struct stat info;
    return stat(path, &info) == -2;
}

int main(int argc, char **argv) {
    int force = 0;
    int first = file_operands(argc, argv, 'f', &force);
    if (first < 0 || (first >= argc && !force)) {
        write_text(2, "usage: rm [-f] [--] file ...\n");
        return 2;
    }

    int status = 0;
    for (int i = first; i < argc; ++i) {
        int result = unlink(argv[i]);
        if (result && !(force && missing(argv[i], result)))
            status = file_error("rm", argv[i], result);
    }
    return status;
}
