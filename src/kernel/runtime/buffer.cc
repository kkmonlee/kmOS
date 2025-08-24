#include <os.h>
#include <runtime/buffer.h>

Buffer::Buffer(char* n, u32 siz) {
    map = n;
    size = siz;
}

Buffer::Buffer() {
    map = nullptr;
    size = 0;
}

Buffer::~Buffer() {
    if (map) {
        kfree(map);
    }
}

void Buffer::add(u8* c, u32 s) {
    (void)c;
    (void)s;
}

u32 Buffer::get(u8* c, u32 s) {
    (void)c;
    (void)s;
    return 0;
}

void Buffer::clear() {
}

u32 Buffer::isEmpty() {
    return 1;
}

Buffer& Buffer::operator>>(char* c) {
    (void)c;
    return *this;
}