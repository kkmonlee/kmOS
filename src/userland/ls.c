#include "user.h"

static u32 length(const char *text) {
    u32 size = 0;
    while (text[size]) ++size;
    return size;
}

static int error(const char *action, const char *path) {
    write_all(2, "ls: ", 4);
    write_all(2, action, length(action));
    write_all(2, ": ", 2);
    write_all(2, path, length(path));
    write_all(2, "\n", 1);
    return 1;
}

static int number(u32 value) {
    char digits[10];
    u32 index = sizeof(digits);
    do {
        digits[--index] = '0' + value % 10;
        value /= 10;
    } while (value);
    return write_all(1, digits + index, sizeof(digits) - index);
}

static int print_long(const struct stat *info, const char *name, const char *path) {
    char mode[] = "---------- ";
    switch (info->st_mode & S_IFMT) {
    case S_IFDIR: mode[0] = 'd'; break;
    case S_IFCHR: mode[0] = 'c'; break;
    case S_IFIFO: mode[0] = 'p'; break;
    case S_IFLNK: mode[0] = 'l'; break;
    }
    const char permissions[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        if (info->st_mode & (0400U >> i)) mode[i + 1] = permissions[i];
    if (info->st_mode & 04000) mode[3] = mode[3] == 'x' ? 's' : 'S';
    if (info->st_mode & 02000) mode[6] = mode[6] == 'x' ? 's' : 'S';
    if (info->st_mode & 01000) mode[9] = mode[9] == 'x' ? 't' : 'T';
    if (write_all(1, mode, sizeof(mode) - 1) || number(info->st_nlink) || puts(" ") ||
        number(info->st_uid) || puts(" ") || number(info->st_gid) || puts(" ") ||
        number(info->st_size) || puts(" ") || puts(name)) return 1;
    if (S_ISLNK(info->st_mode)) {
        char target[4096];
        int count = readlink(path, target, sizeof(target));
        if (count < 0) {
            puts("\n");
            return error("cannot read link", path);
        }
        if (puts(" -> ") || write_all(1, target, count)) return 1;
    }
    return puts("\n");
}

static int list_directory(const char *path, int all, int details) {
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return error("cannot open", path);
    char entry_path[4096];
    u32 prefix = length(path);
    if (details) {
        if (!prefix || prefix >= sizeof(entry_path) - 1) {
            close(fd);
            return error("path too long", path);
        }
        for (u32 i = 0; i < prefix; ++i) entry_path[i] = path[i];
        if (path[prefix - 1] != '/') entry_path[prefix++] = '/';
    }

    struct dirent entries[8];
    int bytes, result = 0;
    while ((bytes = getdents(fd, entries, sizeof(entries))) > 0) {
        u32 count = bytes / sizeof(entries[0]);
        for (u32 i = 0; i < count; ++i) {
            const char *name = entries[i].d_name;
            if (!all && *name == '.') continue;
            if (!details) {
                if (puts(name) || puts("\n")) result = 1;
                continue;
            }
            u32 size = length(name);
            if (size >= sizeof(entry_path) - prefix) {
                result = error("path too long", name);
                continue;
            }
            for (u32 j = 0; j <= size; ++j) entry_path[prefix + j] = name[j];
            struct stat info;
            if (lstat(entry_path, &info)) result = error("cannot stat", entry_path);
            else if (print_long(&info, name, entry_path)) result = 1;
        }
    }
    if (bytes < 0) result = error("cannot read directory", path);
    if (close(fd)) result = 1;
    return result;
}

static int list(const char *path, int all, int details) {
    struct stat info;
    if (lstat(path, &info)) return error("cannot stat", path);
    if (!details && S_ISLNK(info.st_mode) && !stat(path, &info) && S_ISDIR(info.st_mode))
        return list_directory(path, all, details);
    if (S_ISDIR(info.st_mode)) return list_directory(path, all, details);
    if (details) return print_long(&info, path, path);
    return puts(path) || puts("\n");
}

int main(int argc, char **argv) {
    int all = 0, details = 0, first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1]) {
        const char *flag = argv[first++] + 1;
        if (flag[0] == '-' && !flag[1]) break;
        for (; *flag; ++flag) {
            if (*flag == 'a') all = 1;
            else if (*flag == 'l') details = 1;
            else {
                static const char usage[] = "usage: ls [-al] [path ...]\n";
                write_all(2, usage, sizeof(usage) - 1);
                return 2;
            }
        }
    }
    if (first == argc) return list(".", all, details);
    int result = 0;
    for (int i = first; i < argc; ++i) {
        if (argc - first > 1 && ((i > first && puts("\n")) || puts(argv[i]) || puts(":\n"))) return 1;
        if (list(argv[i], all, details)) result = 1;
    }
    return result;
}
