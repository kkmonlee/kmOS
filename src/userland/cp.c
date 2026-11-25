#include "file_command.h"

static int copy_data(int from, int to) {
    char buffer[4096];
    int count;
    while ((count = read(from, buffer, sizeof(buffer))) > 0) {
        for (int offset = 0; offset < count;) {
            int written = write(to, buffer + offset, count - offset);
            if (written <= 0) return written ? written : -5;
            offset += written;
        }
    }
    return count;
}

static int copy_file(const char *source, const char *destination) {
    int from = open(source, O_RDONLY);
    if (from < 0) return file_error("cp", source, from);
    struct stat source_info, target_info;
    int result = fstat(from, &source_info);
    if (!result && !S_ISREG(source_info.st_mode)) result = -22;
    if (result) {
        close(from);
        return file_error("cp", source, result);
    }

    u32 mode = (source_info.st_mode & 0777) | 0200;
    int to = open(destination, O_WRONLY | O_CREAT, mode);
    if (to < 0) {
        close(from);
        return file_error("cp", destination, to);
    }
    result = fstat(to, &target_info);
    int same_file = !result && source_info.st_dev == target_info.st_dev && source_info.st_ino == target_info.st_ino;
    if (!result && !same_file) {
        result = S_ISREG(target_info.st_mode) ? ftruncate(to, 0) : -22;
        if (!result) result = copy_data(from, to);
    }
    int close_result = close(to);
    close(from);
    if (same_file) return file_message("cp", destination, "source and destination are the same file");
    if (!result) result = close_result;
    return result ? file_error("cp", destination, result) : 0;
}

int main(int argc, char **argv) { return transfer_files(argc, argv, "cp", copy_file); }
