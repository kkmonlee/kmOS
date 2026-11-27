#include "file_command.h"

int main(int argc, char **argv) {
    int first = file_operands(argc, argv, 0, 0);
    if (first < 0 || argc - first != 1) {
        write_text(2, "usage: readlink [--] path\n");
        return 2;
    }
    char target[4096];
    int count = readlink(argv[first], target, sizeof(target));
    if (count == -22) return file_message("readlink", argv[first], "not a symbolic link");
    if (count < 0) return file_error("readlink", argv[first], count);
    return write_all(1, target, count) || puts("\n");
}
