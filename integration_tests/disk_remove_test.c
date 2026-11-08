#include "user.h"

#define CHECK(test) do { if (!(test)) fail(__LINE__); } while (0)

static unsigned char buffer[4096];

static void fail(int line) {
    char digits[12];
    int count = 0;
    do { digits[count++] = '0' + line % 10; line /= 10; } while (line);
    puts("disk removal test failed at ");
    while (count) write_all(1, &digits[--count], 1);
    puts("\n");
    exit(1);
}

static int create(const char *path) {
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0);
    return fd;
}

static void contents(const char *path, const char *expected, int size) {
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0 && read(fd, buffer, sizeof(buffer)) == size);
    for (int i = 0; i < size; ++i) CHECK(buffer[i] == (unsigned char)expected[i]);
    CHECK(close(fd) == 0);
}

static void truncation(void) {
    int fd = create("/mnt/disk/truncated");
    for (u32 i = 0; i < sizeof(buffer); ++i) buffer[i] = 0x51;
    for (int i = 0; i < 20; ++i) CHECK(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
    int alias = open("/mnt/disk/truncated", O_RDONLY);
    CHECK(alias >= 0 && ftruncate(fd, 2055) == 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 20 * 4096);
    CHECK(lseek(alias, 0, SEEK_END) == 2055 && ftruncate(alias, 0) == -9);
    CHECK(ftruncate(fd, 20 * 4096) == 0 && lseek(alias, 0, SEEK_SET) == 0);
    for (int offset = 0; offset < 20 * 4096; offset += sizeof(buffer)) {
        CHECK(read(alias, buffer, sizeof(buffer)) == sizeof(buffer));
        for (u32 i = 0; i < sizeof(buffer); ++i)
            CHECK(buffer[i] == (offset + i < 2055 ? 0x51 : 0));
    }
    CHECK(ftruncate(fd, -1) == -22 && ftruncate(1, 0) == -22);
    CHECK(ftruncate(fd, 0) == 0 && lseek(alias, 0, SEEK_END) == 0);
    CHECK(lseek(fd, 0, SEEK_SET) == 0 && write(fd, "again", 5) == 5);
    CHECK(close(alias) == 0 && close(fd) == 0);
    CHECK(open("/mnt/disk/truncated", O_RDONLY | O_TRUNC) == -22);
    fd = open("/mnt/disk/truncated", O_RDWR | O_TRUNC);
    CHECK(fd >= 0 && lseek(fd, 0, SEEK_END) == 0 && close(fd) == 0);
}

static void failed_open(void) {
    int fd = create("/mnt/disk/limit");
    CHECK(write(fd, "keep", 4) == 4 && close(fd) == 0);
    for (int i = 3; i < 32; ++i) CHECK(open("/mnt/disk/limit", O_RDONLY) == i);
    CHECK(open("/mnt/disk/limit", O_RDWR | O_TRUNC) == -24);
    for (int i = 3; i < 32; ++i) CHECK(close(i) == 0);
    contents("/mnt/disk/limit", "keep", 4);
    fd = open("/mnt/disk/protected", O_CREAT | O_EXCL | O_RDWR, 0444);
    CHECK(fd >= 0 && write(fd, "safe", 4) == 4 && close(fd) == 0);
    CHECK(open("/mnt/disk/protected", O_WRONLY | O_TRUNC) == -13);
    contents("/mnt/disk/protected", "safe", 4);
}

static void open_unlink(void) {
    const char *path = "/mnt/disk/recreated";
    int old = create(path);
    CHECK(write(old, "old", 3) == 3 && dup2(old, 31) == 31);
    int alias = open(path, O_RDWR);
    CHECK(alias >= 0 && unlink(path) == 0 && open(path, O_RDONLY) == -2);
    int fresh = create(path);
    CHECK(write(fresh, "new", 3) == 3 && close(fresh) == 0);
    CHECK(lseek(old, 0, SEEK_SET) == 0 && read(old, buffer, 3) == 3);
    CHECK(buffer[0] == 'o' && buffer[1] == 'l' && buffer[2] == 'd');
    CHECK(ftruncate(old, 1) == 0 && lseek(alias, 0, SEEK_END) == 1);
    CHECK(ftruncate(old, 4) == 0 && lseek(alias, 0, SEEK_SET) == 0);
    CHECK(read(alias, buffer, 4) == 4 && buffer[0] == 'o' && !buffer[1] && !buffer[2] && !buffer[3]);
    CHECK(close(old) == 0 && close(31) == 0 && close(alias) == 0);
    contents(path, "new", 3);

    int a = create("/mnt/disk/orphan-a");
    int b = create("/mnt/disk/orphan-b");
    int c = create("/mnt/disk/orphan-c");
    CHECK(write(a, "a", 1) == 1 && write(b, "b", 1) == 1 && write(c, "c", 1) == 1);
    CHECK(unlink("/mnt/disk/orphan-a") == 0 && unlink("/mnt/disk/orphan-b") == 0 &&
          unlink("/mnt/disk/orphan-c") == 0);
    CHECK(close(b) == 0 && close(a) == 0 && close(c) == 0);

    a = create("/mnt/disk/fork-orphan");
    CHECK(write(a, "fork", 4) == 4 && unlink("/mnt/disk/fork-orphan") == 0);
    int child = fork();
    CHECK(child >= 0);
    if (!child) {
        CHECK(lseek(a, 0, SEEK_SET) == 0 && read(a, buffer, 4) == 4);
        CHECK(buffer[0] == 'f' && buffer[3] == 'k' && close(a) == 0);
        exit(0);
    }
    int status;
    CHECK(close(a) == 0 && waitpid(child, &status, 0) == child && !status);
}

static void existing_files(void) {
    int fd = open("/mnt/disk/hard-a", O_RDONLY);
    CHECK(fd >= 0 && unlink("/mnt/disk/hard-a") == 0 && unlink("/mnt/disk/hard-b") == 0);
    CHECK(read(fd, buffer, 4) == 4 && buffer[0] == 'h' && buffer[3] == 'd');
    int fresh = create("/mnt/disk/hard-b");
    CHECK(write(fresh, "brand", 5) == 5 && close(fresh) == 0 && close(fd) == 0);
    fd = open("/mnt/disk/attr-a", O_RDONLY);
    if (fd >= 0) {
        CHECK(close(fd) == 0 && unlink("/mnt/disk/attr-a") == 0 && unlink("/mnt/disk/attr-b") == 0);
    } else {
        CHECK(fd == -2);
    }
}

int main(void) {
    int done = open("/mnt/disk/done", O_RDONLY);
    if (done >= 0) {
        CHECK(close(done) == 0);
        contents("/mnt/disk/truncated", "", 0);
        contents("/mnt/disk/recreated", "new", 3);
        contents("/mnt/disk/hard-b", "brand", 5);
        CHECK(open("/mnt/disk/hard-a", O_RDONLY) == -2 &&
              open("/mnt/disk/attr-a", O_RDONLY) == -2 &&
              open("/mnt/disk/attr-b", O_RDONLY) == -2);
        CHECK(unlink("/mnt/disk/recreated") == -30);
        puts("truncation and removal verified after reboot\n");
        return 0;
    }
    CHECK(done == -2 && unlink("/mnt/disk/missing") == -2);
    CHECK(unlink("/mnt/disk/lost+found") == -21);
    truncation();
    failed_open();
    open_unlink();
    existing_files();
    done = create("/mnt/disk/done");
    CHECK(close(done) == 0);
    puts("truncation, unlink and final close verified\n");
    return 0;
}
