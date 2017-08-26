//
// Created by hampe on 09 May 2017.
//

#ifndef KMOS_BUFFER_HPP
#define KMOS_BUFFER_HPP


class Buffer {
public:
    Buffer(char* n, u32 size);
    Buffer();
    ~Buffer();

    void add(u8* c, u32 s);
    u32 get(u8* c, u32 s);
    void clear();
    u32 isEmpty();

    Buffer &operator>>(char* c);

    u32 size;
    char* map;
};

#endif //KMOS_BUFFER_HPP
