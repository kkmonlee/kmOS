#include <os.h>

Io* Io::last_io = &io;
Io* Io::current_io = &io;

// Video memory
char* Io::vidMem = (char*)RAMSCREEN;

Io::Io() {
    real_screen = (char*)RAMSCREEN;
}

Io::Io(u32 flag) {
    real_screen = (char*)screen;
}

// Output byte
void Io::outb(u32 ad, u8 v) {
    asmv("outb %%al, %%dx" :: "d" (ad), "a" (v));;
}

/* Output word */
void Io::outw(u32 ad, u16 v) {
    asmv("outw %%ax, %%dx" :: "d" (ad), "a" (v));
}

/* Output word */
void Io:outl(u32 ad, u32 v) {
    asmv("outl %%eax, %%dx" :: "d" (ad), "a" (v));
}

/* Input byte */
u8 Io::inb(u32 ad) {
    u8 _v;
    asmv("inb %%dx, %%al" : "=a" (_v) : "d" (ad)); \
    return _v;
}

/* Input word */
u16 Io::inw(u32 ad) {
    u16 _v;
    asmv("inw %%dx, %%ax" : "=a" (_v) : "d" (ad));
    return _v;
}

/* Input word */
u32 Io:inl(u32 ad) {
    u32 _v;
    asmv("inl %%dx, %%eax" : "=a" (_v) : "d" (ad)); \
    return _v;
}

u32 Io::getX() {
    return (u32)x;
}

u32 Io:getY() {
    return (u32)y;
}

void Io::scrollUp(unsigned int n) {
    unsigned char *video, *tmp;

    for (video = (unsigned char *) real_screen;
            video < (unsigned char *) SCREENLIM; video += 2) {
        tmp = (unsigned char *) (video + n * 160);

        if (tmp < (unsigned char *) SCREENLIM) {
            *video = *tmp;
            *(video + 1) = *(tmp + 1);
        } else {
            *video = 0;
            *(video + 1) = 0x07;
        }
    }

    y -= n;
    if (y < 0) y = 0;
}

void Io::save_screen() {
    memcpy(screen, (char*)RAMSCREEN, SIZESCREEN);
    real_screen = (char*)screen;
}

void Io::load_screen() {
    memcpy((char*)RAMSCREEN, screen, SIZESCREEN);
    real_screen = (char*)RAMSCREEN;
}

void Io:switchtty() {
    current_io->save_screen();
    load_screen();
    last_io = current_io;
    current_io = this;
}


