#include "user.h"

#define CHECK(test) do { if (!(test)) { puts("failed: " #test "\n"); return 1; } } while (0)

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

int main(void) {
    struct stat info;
    struct dirent entries[20];
    CHECK(stat("/", &info) == 0 && S_ISDIR(info.st_mode));
    int root = open("/", O_RDONLY | O_DIRECTORY);
    CHECK(root >= 0 && getdents(root, entries, sizeof(entries[0])) == sizeof(entries[0]));
    CHECK(equal(entries[0].d_name, ".") && entries[0].d_ino == info.st_ino);
    int copy = dup(root);
    CHECK(copy >= 0 && getdents(copy, entries, sizeof(entries[0])) == sizeof(entries[0]));
    CHECK(equal(entries[0].d_name, "..") && entries[0].d_ino == info.st_ino);
    CHECK(lseek(copy, 0, SEEK_SET) == 0 && close(copy) == 0);
    CHECK(getdents(root, entries, 1) == -22 && read(root, entries, 1) == -21);
    CHECK(getdents(root, entries, sizeof(entries[0])) == sizeof(entries[0]) && equal(entries[0].d_name, "."));
    CHECK(close(root) == 0);

    CHECK(chdir("/mnt/disk") == 0 && mkdir("sample", 0755) == 0 && chdir("sample") == 0);
    int fd = open("notes with spaces", O_CREAT | O_RDWR, 0640);
    CHECK(fd >= 0 && write(fd, "abc", 3) == 3);
    CHECK(stat("notes with spaces", &info) == 0 && S_ISREG(info.st_mode));
    CHECK((info.st_mode & 0777) == 0640 && info.st_size == 3 && info.st_nlink == 1);
    CHECK(info.st_blocks > 0 && info.st_dev != 0 && info.st_blksize >= 1024);
    unsigned int inode = info.st_ino;
    CHECK(fstat(fd, &info) == 0 && info.st_ino == inode && info.st_size == 3);
    CHECK(getdents(fd, entries, sizeof(entries)) == -20 && close(fd) == 0);
    CHECK(open("notes with spaces", O_RDONLY | O_DIRECTORY) == -20);
    fd = open(".hidden", O_CREAT | O_WRONLY, 0600);
    CHECK(fd >= 0 && close(fd) == 0);
    for (int i = 0; i < 20; ++i) {
        char name[] = "file00";
        name[4] += i / 10;
        name[5] += i % 10;
        fd = open(name, O_CREAT | O_WRONLY, 0644);
        CHECK(fd >= 0 && close(fd) == 0);
    }

    int directory = open(".", O_RDONLY | O_DIRECTORY);
    CHECK(directory >= 0 && getdents(directory, entries, 3 * sizeof(entries[0])) == 3 * sizeof(entries[0]));
    CHECK(equal(entries[2].d_name, "file19"));
    CHECK(unlink("file19") == 0);
    fd = open("new", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0 && close(fd) == 0);
    int bytes, total = 0, notes = 0;
    while ((bytes = getdents(directory, entries, sizeof(entries))) > 0) {
        CHECK(bytes % sizeof(entries[0]) == 0);
        for (unsigned int i = 0; i < bytes / sizeof(entries[0]); ++i) {
            CHECK(!equal(entries[i].d_name, "new") && !equal(entries[i].d_name, "file19"));
            if (equal(entries[i].d_name, "notes with spaces")) {
                CHECK(entries[i].d_ino == inode && entries[i].d_type == DT_REG);
                ++notes;
            }
            ++total;
        }
    }
    CHECK(bytes == 0 && total == 21 && notes == 1 && close(directory) == 0);
    CHECK(mkdir("gone", 0755) == 0);
    directory = open("gone", O_RDONLY | O_DIRECTORY);
    CHECK(directory >= 0 && rmdir("gone") == 0);
    CHECK(fstat(directory, &info) == 0 && S_ISDIR(info.st_mode) && info.st_nlink == 0);
    CHECK(getdents(directory, entries, sizeof(entries)) == -2 && close(directory) == 0);
    puts("directory and metadata syscalls verified\n");
    return 0;
}
