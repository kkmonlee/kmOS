#include "file_command.h"

static int move_file(const char *source, const char *destination) {
    int result = rename(source, destination);
    return result ? file_error("mv", source, result) : 0;
}

int main(int argc, char **argv) { return transfer_files(argc, argv, "mv", move_file); }
