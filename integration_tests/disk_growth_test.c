#include "user.h"

#define CHECK(test) do { if (!(test)) fail(__LINE__); } while (0)

static unsigned char buffer[4096];
static const u32 contents_size = 20 * 4096 + 17;
static const u32 tail_offset = 32 * 4096 + 5;

static void fail(int line) {
    char digits[12];
    int count = 0;
    do { digits[count++] = '0' + line % 10; line /= 10; } while (line);
    puts("disk growth test failed at ");
    while (count) write_all(1, &digits[--count], 1);
    puts("\n");
    exit(1);
}

static unsigned char pattern(u32 offset) { return (offset * 37 + 11) % 251; }

static void path_for(int index, char *path) {
    const char prefix[] = "/mnt/disk/entry-";
    u32 pos = 0;
    for (; pos < sizeof(prefix) - 1; ++pos) path[pos] = prefix[pos];
    path[pos++] = '0' + index / 100;
    path[pos++] = '0' + index / 10 % 10;
    path[pos++] = '0' + index % 10;
    const char suffix[] = "-directory-growth";
    for (u32 i = 0; i < sizeof(suffix); ++i) path[pos + i] = suffix[i];
}

static void maximum_name(char *path) {
    const char prefix[] = "/mnt/disk/";
    for (u32 i = 0; i < sizeof(prefix) - 1; ++i) path[i] = prefix[i];
    for (u32 i = 0; i < 255; ++i) path[sizeof(prefix) - 1 + i] = 'a' + i % 26;
    path[sizeof(prefix) - 1 + 255] = 0;
}

static void concurrent(void) {
    int fd = open("/mnt/disk/shared", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0);
    int child = fork();
    CHECK(child >= 0);
    char value = child ? 'p' : 'c';
    for (int i = 0; i < 50; ++i) CHECK(write(fd, &value, 1) == 1);
    if (!child) exit(0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && !status);
    CHECK(lseek(fd, 0, SEEK_END) == 100 && lseek(fd, 0, SEEK_SET) == 0);
    CHECK(read(fd, buffer, 100) == 100);
    int parents = 0, children = 0;
    for (int i = 0; i < 100; ++i) {
        if (buffer[i] == 'p') ++parents;
        else if (buffer[i] == 'c') ++children;
    }
    CHECK(parents == 50 && children == 50 && close(fd) == 0);
}

static void verify(int fd) {
    CHECK(lseek(fd, 0, SEEK_END) == (int)tail_offset + 4);
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    for (u32 offset = 0; offset < tail_offset + 4;) {
        int count = read(fd, buffer, sizeof(buffer));
        CHECK(count > 0);
        for (int i = 0; i < count; ++i) {
            u32 position = offset + i;
            unsigned char expected = position < contents_size ? pattern(position) :
                (position < tail_offset ? 0 : (unsigned char)"tail"[position - tail_offset]);
            CHECK(buffer[i] == expected);
        }
        offset += count;
    }
    CHECK(read(fd, buffer, 1) == 0 && lseek(fd, -1, SEEK_SET) == -22);
    CHECK(lseek(fd, 0, 3) == -22 && lseek(1, 0, SEEK_SET) == -29);
}

int main(void) {
    char maximum[266];
    maximum_name(maximum);
    const char *name = "/mnt/disk/created";
    int fd = open(name, O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd == -30 || fd == -17) {
        fd = open(name, O_RDONLY);
        CHECK(fd >= 0);
        verify(fd);
        CHECK(close(fd) == 0);
        for (int i = 0; i < 70; ++i) {
            char path[64];
            path_for(i, path);
            fd = open(path, O_RDONLY);
            CHECK(fd >= 0 && read(fd, buffer, 1) == 0 && close(fd) == 0);
        }
        fd = open(maximum, O_RDONLY);
        CHECK(fd >= 0 && close(fd) == 0);
        fd = open("/mnt/disk/lost+found/nested", O_RDONLY);
        CHECK(fd >= 0 && read(fd, buffer, sizeof(buffer)) == 6 && close(fd) == 0);
        puts("created files verified after reboot\n");
        return 0;
    }

    CHECK(fd >= 0 && open(name, O_CREAT | O_EXCL | O_RDWR, 0644) == -17);
    for (u32 offset = 0; offset < contents_size;) {
        u32 count = contents_size - offset;
        if (count > sizeof(buffer)) count = sizeof(buffer);
        for (u32 i = 0; i < count; ++i) buffer[i] = pattern(offset + i);
        CHECK(write(fd, buffer, count) == (int)count);
        offset += count;
    }
    CHECK(lseek(fd, tail_offset, SEEK_SET) == (int)tail_offset && write(fd, "tail", 4) == 4);
    verify(fd);
    CHECK(close(fd) == 0);

    for (int i = 0; i < 70; ++i) {
        char path[64];
        path_for(i, path);
        fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
        CHECK(fd >= 0 && close(fd) == 0);
    }
    fd = open(maximum, O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0 && close(fd) == 0);
    fd = open("/mnt/disk/lost+found/nested", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0 && write(fd, "nested", 6) == 6 && close(fd) == 0);
    concurrent();
    CHECK(chdir("/mnt/disk") == 0);
    fd = open("mode-zero", O_CREAT | O_EXCL | O_RDWR, 0);
    CHECK(fd >= 0 && write(fd, "mode", 4) == 4 && close(fd) == 0 &&
          open("mode-zero", O_WRONLY) == -13);
    fd = open("name?:*with-characters", O_CREAT | O_EXCL | O_RDWR, 0644);
    CHECK(fd >= 0 && write(fd, "name", 4) == 4 && close(fd) == 0);
    CHECK(open("name?:*with-characters", O_RDONLY) >= 0);
    for (int i = 4; i < 32; ++i) CHECK(open("created", O_RDONLY) == i);
    CHECK(open("no-slot", O_CREAT | O_RDWR, 0644) == -24);
    CHECK(close(31) == 0 && open("no-slot", O_RDONLY) == -2);
    CHECK(chdir("/") == 0);
    CHECK(open("/etc/readonly-new", O_CREAT | O_RDWR, 0644) == -30);
    puts("file creation, growth and sparse writes verified\n");
    return 0;
}
