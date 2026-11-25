#include "user.h"

#define CHECK(test) do { if (!(test)) fail(__LINE__); } while (0)

static void fail(int line) {
    char digits[12];
    int count = 0;
    do { digits[count++] = '0' + line % 10; line /= 10; } while (line);
    puts("directory test failed at ");
    while (count) write_all(1, &digits[--count], 1);
    puts("\n");
    exit(1);
}

static void held_directories(void) {
    CHECK(mkdir("/mnt/disk/held", 0755) == 0 && mkdir("/mnt/disk/held/sub", 0755) == 0);
    CHECK(chdir("/mnt/disk/held/sub") == 0);
    CHECK(rmdir("/mnt/disk/held/sub") == 0 && rmdir("/mnt/disk/held") == 0);
    char path[64];
    CHECK(getcwd(path, sizeof(path)) == -2);
    CHECK(mkdir("unreachable", 0755) == -2);
    CHECK(open("unreachable", O_CREAT | O_RDWR, 0644) == -2);
    CHECK(chdir("..") == 0 && mkdir("unreachable", 0755) == -2);
    CHECK(chdir("..") == 0);
    CHECK(mkdir("held", 0755) == 0 && rmdir("held") == 0);
    CHECK(mkdir("fork-cwd", 0755) == 0 && chdir("fork-cwd") == 0);

    int ends[2];
    CHECK(pipe(ends) == 0);
    int child = fork();
    CHECK(child >= 0);
    if (!child) {
        char value;
        CHECK(close(ends[1]) == 0 && read(ends[0], &value, 1) == 1);
        CHECK(chdir("..") == 0 && close(ends[0]) == 0);
        exit(0);
    }
    CHECK(close(ends[0]) == 0 && rmdir("/mnt/disk/fork-cwd") == 0);
    CHECK(chdir("..") == 0 && write(ends[1], "x", 1) == 1 && close(ends[1]) == 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && !status);
    CHECK(mkdir("fork-cwd", 0755) == 0 && rmdir("fork-cwd") == 0);
    CHECK(chdir("/") == 0);
}

static void deep_removed_directories(void) {
    char path[600] = "/mnt/disk/deep";
    int length = 0;
    while (path[length]) ++length;
    int base = length;
    CHECK(mkdir(path, 0755) == 0);
    for (int i = 0; i < 256; ++i) {
        path[length++] = '/';
        path[length++] = 'd';
        path[length] = 0;
        CHECK(mkdir(path, 0755) == 0);
    }
    CHECK(chdir(path) == 0);
    for (;;) {
        CHECK(rmdir(path) == 0);
        if (length == base) break;
        length -= 2;
        path[length] = 0;
    }
    CHECK(chdir("/") == 0);
}

static void rename_entries(void) {
    CHECK(chdir("/mnt/disk") == 0 && mkdir("moves", 0755) == 0 && chdir("moves") == 0);
    struct stat identity, info;
    CHECK(rename("../rename-link", "../rename-alias") == 0);
    CHECK(stat("../rename-link", &identity) == 0 && stat("../rename-alias", &info) == 0 &&
          identity.st_ino == info.st_ino && info.st_nlink == 2);
    int source = open("source", O_CREAT | O_RDWR, 0644);
    int target = open("target", O_CREAT | O_RDWR, 0644);
    CHECK(source >= 0 && target >= 0 && write(source, "new", 3) == 3 && write(target, "old", 3) == 3);
    CHECK(fstat(source, &identity) == 0);
    CHECK(rename("source", "source") == 0);
    CHECK(rename("source", "/sys/env/source") == -18);
    CHECK(rename("source/", "target") == -20 && rename("source", "target/") == -20);
    CHECK(rename("source", "target") == 0 && stat("source", &info) == -2);
    CHECK(stat("target", &info) == 0 && info.st_ino == identity.st_ino && info.st_size == 3);
    CHECK(fstat(target, &info) == 0 && info.st_ino != identity.st_ino && info.st_nlink == 0);
    char bytes[3];
    CHECK(lseek(source, 0, SEEK_SET) == 0 && read(source, bytes, 3) == 3 && bytes[0] == 'n');
    CHECK(lseek(target, 0, SEEK_SET) == 0 && read(target, bytes, 3) == 3 && bytes[0] == 'o');
    CHECK(close(source) == 0 && close(target) == 0);

    CHECK(mkdir("a", 0755) == 0 && mkdir("b", 0755) == 0);
    CHECK(mkdir("a/child", 0755) == 0 && mkdir("b/empty", 0755) == 0);
    CHECK(rename("a", "a/child/loop") == -22 && rename("b/empty", "a") == -39);
    CHECK(rename("target", "a") == -21 && rename("a", "target") == -20);
    int held = open("b/empty", O_RDONLY | O_DIRECTORY);
    CHECK(held >= 0 && chdir("a/child") == 0);
    CHECK(rename("../child", "../../b/empty") == 0);
    char path[64];
    const char moved[] = "/mnt/disk/moves/b/empty";
    CHECK(getcwd(path, sizeof(path)) == sizeof(moved));
    for (u32 i = 0; i < sizeof(moved); ++i) CHECK(path[i] == moved[i]);
    CHECK(stat(".", &identity) == 0 && stat("/mnt/disk/moves/b/empty", &info) == 0 &&
          info.st_ino == identity.st_ino);
    CHECK(fstat(held, &info) == 0 && info.st_nlink == 0 && close(held) == 0);
    CHECK(chdir("..") == 0 && stat("empty", &info) == 0 && info.st_ino == identity.st_ino);
    CHECK(stat(".", &info) == 0 && info.st_nlink == 3);
    CHECK(stat("../a", &info) == 0 && info.st_nlink == 2);
    CHECK(rename("/mnt/disk", "/mnt/other") == -16 && chdir("/") == 0);
}

int main(void) {
    int done = open("/mnt/disk/done", O_RDONLY);
    if (done >= 0) {
        CHECK(close(done) == 0 && chdir("/mnt/disk/saved") == 0);
        int fd = open("file", O_RDONLY);
        char buffer[5];
        CHECK(fd >= 0 && read(fd, buffer, sizeof(buffer)) == sizeof(buffer));
        CHECK(buffer[0] == 's' && buffer[4] == 'd' && close(fd) == 0);
        CHECK(chdir("/") == 0 && chdir("/mnt/disk/tree") == -2);
        CHECK(mkdir("/mnt/disk/new", 0755) == -30 && rmdir("/mnt/disk/saved") == -30);
        CHECK(stat("/mnt/disk/moves/b/empty", &(struct stat){0}) == 0);
        CHECK(chdir("/mnt/disk/moves/b/empty/..") == 0);
        char path[64];
        CHECK(getcwd(path, sizeof(path)) == sizeof("/mnt/disk/moves/b"));
        CHECK(rename("empty", "../renamed") == -30 && chdir("/") == 0);
        int moved = open("/mnt/disk/moves/target", O_RDONLY);
        CHECK(moved >= 0 && read(moved, buffer, sizeof(buffer)) == 3 &&
              buffer[0] == 'n' && close(moved) == 0);
        puts("directories verified after reboot\n");
        return 0;
    }
    CHECK(done == -2 && rmdir("/") == -16 && rmdir("/mnt/disk") == -16);
    CHECK(rmdir("/mnt/disk/.") == -22 && rmdir("/mnt/disk/..") == -22);
    CHECK(rmdir("/mnt/disk/missing") == -2);
    CHECK(mkdir("/mnt/disk/tree///", 0755) == 0 && mkdir("/mnt/disk/tree", 0755) == -17);
    CHECK(mkdir("/mnt/disk/tree/child", 0755) == 0);
    CHECK(rmdir("/mnt/disk/tree") == -39);
    int fd = open("/mnt/disk/tree/child/file", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0 && close(fd) == 0 && rmdir("/mnt/disk/tree/child") == -39);
    CHECK(rmdir("/mnt/disk/tree/child/file") == -20);
    CHECK(unlink("/mnt/disk/tree/child/file") == 0);
    CHECK(rmdir("/mnt/disk/tree/child///") == 0 && rmdir("/mnt/disk/tree") == 0);
    CHECK(rmdir("/mnt/disk/has-link") == -39);
    CHECK(rmdir("/mnt/disk/lost+found") == 0);
    held_directories();
    deep_removed_directories();
    rename_entries();

    char name[266] = "/mnt/disk/";
    for (int i = 0; i < 255; ++i) name[10 + i] = 'a' + i % 26;
    name[265] = 0;
    CHECK(mkdir(name, 0755) == 0 && rmdir(name) == 0);
    CHECK(mkdir("/mnt/disk/readonly", 0500) == 0);
    CHECK(mkdir("/mnt/disk/readonly/child", 0755) == -13);
    CHECK(rmdir("/mnt/disk/readonly") == 0);
    CHECK(mkdir("/mnt/disk/saved", 0755) == 0 && chdir("/mnt/disk/saved") == 0);
    fd = open("file", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0 && write(fd, "saved", 5) == 5 && close(fd) == 0);
    CHECK(chdir("/") == 0);
    done = open("/mnt/disk/done", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(done >= 0 && close(done) == 0);
    puts("mkdir, rmdir and removed working directories verified\n");
    return 0;
}
