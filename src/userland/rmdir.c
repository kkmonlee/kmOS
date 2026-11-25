#include "file_command.h"

int main(int argc, char **argv) {
    int first = file_operands(argc, argv, 0, 0);
    if (first < 0 || first >= argc) {
        write_text(2, "usage: rmdir [--] directory ...\n");
        return 2;
    }

    int status = 0;
    for (int i = first; i < argc; ++i) {
        int result = rmdir(argv[i]);
        if (result) status = file_error("rmdir", argv[i], result);
    }
    return status;
}
