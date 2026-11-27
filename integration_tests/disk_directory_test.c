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

static void check_link(const char *path, const char *target) {
    char buffer[4096];
    int size = 0;
    while (target[size]) ++size;
    buffer[size] = '!';
    CHECK(readlink(path, buffer, sizeof(buffer)) == size && buffer[size] == '!');
    for (int i = 0; i < size; ++i) CHECK(buffer[i] == target[i]);
}

static void check_byte(const char *path, char value) {
    int fd = open(path, O_RDONLY);
    char byte;
    CHECK(fd >= 0 && read(fd, &byte, 1) == 1 && byte == value && close(fd) == 0);
}

static void verify_links(void) {
    CHECK(chdir("/mnt/disk") == 0);
    CHECK(mkdir("links", 0755) == 0 && mkdir("links/sub", 0755) == 0);
    CHECK(chdir("links") == 0);
    int fd = open("data", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0 && write(fd, "a", 1) == 1);
    CHECK(link("data", "sub/data") == 0 && link("data", "sub/data") == -17);
    CHECK(link("data", "/sys/env/data") == -18 && link("sub", "dir-hardlink") == -1);
    struct stat original, alias;
    CHECK(fstat(fd, &original) == 0 && stat("sub/data", &alias) == 0);
    CHECK(original.st_ino == alias.st_ino && alias.st_nlink == 2);
    int second = open("sub/data", O_WRONLY | O_APPEND);
    CHECK(second >= 0 && write(second, "b", 1) == 1 && close(second) == 0);
    CHECK(unlink("data") == 0 && fstat(fd, &alias) == 0 && alias.st_nlink == 1 && alias.st_size == 2);
    char bytes[2];
    CHECK(lseek(fd, 0, SEEK_SET) == 0 && read(fd, bytes, 2) == 2 && bytes[0] == 'a' && bytes[1] == 'b');
    CHECK(close(fd) == 0);

    CHECK(symlink("sub/data", "relative") == 0);
    CHECK(symlink("/mnt/disk/links/sub/data", "absolute") == 0);
    CHECK(symlink("sub", "directory") == 0);
    CHECK(link("relative", "relative-alias") == 0);
    CHECK(lstat("relative", &original) == 0 && lstat("relative-alias", &alias) == 0);
    CHECK(S_ISLNK(original.st_mode) && original.st_blocks == 0 && alias.st_nlink == 2 &&
          original.st_ino == alias.st_ino);
    CHECK(stat("relative", &alias) == 0 && S_ISREG(alias.st_mode) && alias.st_ino != original.st_ino);
    CHECK(lstat("directory/", &alias) == 0 && S_ISDIR(alias.st_mode));
    CHECK(rmdir("directory") == -20 && unlink("directory/") == -20);
    CHECK(unlink("sub/") == -21 && unlink("missing/") == -2);
    CHECK(readlink("relative", bytes, sizeof(bytes)) == 2 && bytes[0] == 's' && bytes[1] == 'u');
    CHECK(readlink("sub/data", bytes, 2) == -22 && readlink("relative", bytes, 0) == -22);
    CHECK(symlink("../relative", "sub/up") == 0);
    check_byte("directory/up", 'a');
    check_byte("absolute", 'a');
    check_link("../has-link/unknown", "target");

    char boundary[61];
    for (u32 i = 0; i < sizeof(boundary); ++i) boundary[i] = 'a';
    boundary[59] = 0;
    CHECK(symlink(boundary, "inline-limit") == 0);
    CHECK(lstat("inline-limit", &alias) == 0 && alias.st_size == 59 && alias.st_blocks == 0);
    check_link("inline-limit", boundary);
    boundary[59] = 'a';
    boundary[60] = 0;
    CHECK(symlink(boundary, "block-minimum") == 0);
    CHECK(lstat("block-minimum", &alias) == 0 && alias.st_size == 60 && alias.st_blocks > 0);
    check_link("block-minimum", boundary);

    char long_target[1025];
    for (u32 i = 0; i < sizeof(long_target) - 1; i += 2) {
        long_target[i] = '.';
        long_target[i + 1] = '/';
    }
    long_target[1024] = 0;
    CHECK(stat(".", &alias) == 0);
    if (alias.st_blksize == 1024) CHECK(symlink(long_target, "too-long") == -36);
    long_target[1023] = 0;
    CHECK(symlink(long_target, "long") == 0);
    CHECK(lstat("long", &alias) == 0 && alias.st_size == 1023 && alias.st_blocks > 0);
    check_link("long", long_target);
    check_byte("long/sub/data", 'a');
    CHECK(symlink(long_target, "remove-long") == 0 && unlink("remove-long") == 0);
    CHECK(symlink("sub/data", "remove-short") == 0 && unlink("remove-short") == 0);

    CHECK(symlink("created", "dangling") == 0);
    CHECK(lstat("dangling", &alias) == 0 && stat("dangling", &alias) == -2);
    CHECK(open("dangling", O_CREAT | O_EXCL | O_RDWR, 0644) == -17);
    fd = open("dangling", O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0 && write(fd, "c", 1) == 1 && close(fd) == 0);
    check_byte("created", 'c');
    CHECK(symlink("loop2", "loop1") == 0 && symlink("loop1", "loop2") == 0);
    CHECK(stat("loop1", &alias) == -40 && open("loop2", O_RDONLY) == -40);
    check_link("loop1", "loop2");
    CHECK(rename("relative", "created") == 0 && lstat("created", &alias) == 0 && S_ISLNK(alias.st_mode));
    CHECK(rename("sub/data", "absolute") == 0 && lstat("absolute", &alias) == 0 && S_ISREG(alias.st_mode));
    CHECK(link("absolute", "persistent") == 0);
    CHECK(rename("created", "sub/data") == 0 && unlink("relative-alias") == 0);
    CHECK(lstat("sub/data", &alias) == 0 && alias.st_nlink == 1 && stat("sub/data", &alias) == -2);
    CHECK(chdir("/") == 0);
}

static void verify_persisted_links(void) {
    CHECK(chdir("/mnt/disk/links") == 0);
    struct stat info, original;
    CHECK(lstat("sub/data", &info) == 0 && S_ISLNK(info.st_mode) && info.st_nlink == 1);
    check_link("sub/data", "sub/data");
    CHECK(stat("sub/data", &info) == -2 && stat("loop1", &info) == -40);
    CHECK(lstat("long", &info) == 0 && info.st_size == 1023 && info.st_blocks > 0);
    check_byte("long/absolute", 'a');
    CHECK(stat("absolute", &original) == 0 && stat("persistent", &info) == 0 &&
          original.st_ino == info.st_ino && info.st_nlink == 2);
    CHECK(link("absolute", "denied-hard") == -30 && symlink("absolute", "denied-sym") == -30);
    CHECK(chdir("/") == 0);
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
        verify_persisted_links();
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
    verify_links();

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
