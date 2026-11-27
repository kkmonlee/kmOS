#include "file_command.h"

static int hard_link(const char *source, const char *destination) {
    int result = link(source, destination);
    if (result == -18) return file_message("ln", destination, "cross-filesystem link");
    return result ? file_error("ln", destination, result) : 0;
}

static int symbolic_link(const char *target, const char *destination) {
    int result = symlink(target, destination);
    return result ? file_error("ln", destination, result) : 0;
}

int main(int argc, char **argv) {
    int symbolic = 0;
    int first = file_operands(argc, argv, 's', &symbolic);
    if (first < 0 || argc - first < 2) {
        write_text(2, "usage: ln [-s] [--] source ... destination\n");
        return 2;
    }
    return transfer_paths(argc, argv, first, "ln", symbolic ? symbolic_link : hard_link);
}
