#include <os.h>

IO* IO::last_io = &io;
IO* IO::current_io = &io;

char* IO::vidmem = (char*)RAMSCREEN

IO::IO() {
    real_screen = (char*)RAMSCREEN;
}

IO::IO(u32 flag) {
       real_screen = (char*)screen;
}

void IO::outb(u32 ad, u8 v) {
    asmv("outb %%al, %%dx" :: "d" (ad), "a" (v));;
}

void IO::outw(u32 ad, u16 v) {
    asmv("outw %%ax, %%dx" :: "d" (ad), "a" (v));
}

void IO::outl(u32 ad, u32 v) {
    asmv("outl %%eax, %%dx", :: "d" (ad), "a" (v));
}

u8 IO::inb(u32 ad) {
    u8 _v;
    asmv("inb %%dx, %%al" : "=a" (_v) : "d" (ad));
    return _v;
}


u16 IO::inw(u32 ad) {
    u16 _v;
    asmv("inw %%dx, %%ax" : "=a" (_v) : "d" (ad));
    return _v;
}

u32 IO::inl(u32 ad) {
    u32 _v;
    asmv("inl %%dx, %%eax", : "=a" (_v) : "d" (ad));
    return _v;
}

u32 IO:getX() {
    return (u32)x;
}

u32 IO::getY() {
    return (u32)y;
}

void IO::scrollup(unsigned int n) {
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

void IO::save_screen() {
    memcpy(screen, (char*)RAMSCREEN, SIZESCREEN);
    real_screen = (char*)screen;
}

void IO::load_screen() {
    memcpy((char*)RAMSCREEN, screen, SIZESCREEN);
    real_screen = (char*)RAMSCREEN;
}

void IO::switchtty() {
    current_io->save_screen();
    load_screen();
    last_io = current_io;
    current_io = this;
}

void IO::putc(char c) {
    kattr = 0x07;
    unsigned char *video;
    video = (unsigned char *) (real_screen + 2 * x + 160 * y);
    if (c == 10) {
        x = 0;
        y++;
    } else if (c == 8) {
        if (x) *(video + 1) = 0x0;
        x--;
    } else if (c == 9) {
        x = x + 8 - (x % 8);
    } else if (c == 13) {
        x = 0;
    } else {
        *video = c;
        *(video + 1) = kattr;
        x++;
        if (x > 79) {
            x = 0;
            y++;
        }
    }

    if (y > 24) scrollup(y - 24);
}

void IO::setColor(char fcol, char bcol) {
    fcolor = fcol;
    bcolor = bcol;
}

void IO::setXY(char xc, char yc) {
    x = xc;
    y = yc;
}

void IO::clear() {
    x = 0;
    y = 0;
    memset((char *)RAMSCREEN, 0, SIZESCREEN);
}

void IO::print(const char *s, ...) {
    va_list ap;
    
    char buf[16];
    int i, j, size, buflen, neg;

    unsigned char c;
    int ival;
    unsigned int uival;

    va_start(ap, s);

    while ((c = *s++)) {
        size = 0, neg = 0;

        if (c == 0) break;
        else if (c == '%') {
            c = *s++;
            if (c >= '0' && c <= '9') {
                size = c - '0';
                c = *s++;
            }

            if (c == 'd') {
                ival = va_arg(ap, int);
                if (ival < 0) {
                    uival = 0 - ival;
                    neg++;
                } else uival = ival;
                itoa(buf, uival, 10);

                buflen = strlen(buf);

                if (buflen < size)
                    for (i = size, j = buflen; i >= 0; i--, j--)
                        buf[i] = (j >= 0) ? buf[j] : '0';

                if (neg) print("-%s", buf);
                else print(buf);
            } else if (c == 'u') {
                uival = va_arg(ap, int);
                itoa(buf, uival, 10);

                buflen = strlen(buf);
                if (buflen < size)
                    for (i = size, j = buflen; i >= 0; i--, j--)
                        buf[i] = (j >= 0) ? buf[j] : '0';
                print(buf);
            } else if (c == 'x' || c == 'X') {
                uival = va_arg(ap, int);
                itoa(buf, uival, 16);

                buflen = strlen(buf);
                if (buflen < size)
                    for (i = size, j = buflen; i >= 0; i--, j--)
                        buf[i] = (j >= 0) ? buf[j] : '0';
                print("0x%s", buf);
            } else if (c == 'p') {
                uival = va_arg(ap, int);
                itoa(buf, uival, 16);
                size = 8;

                buflen = strlen(buf);
                if (buflen < size)
                    for (i = size, j = buflen; i >= 0; i--, j--)
                        buf[i] = (j >= 0) ? buf[j] : '0';
                print("0x%s", buf);
            } else if (c == 's') {
                print((char *) va_arg(ap, int));
            }
        } else {
            putc(c);
        }
    }

    return;
}

void IO::putctty(char c) {
    if (keystate == BUFFERED) {
        if (c == 8) {
            if (keypos > 0) {
                inbuf[keypos--] = 0;
            }
        } else if (c == 10) {
            inbuf[keypos++] = c;
            inbuf[keypos] = 0;
            inlock = 0;
            keypos = 0;
        } else {
            inbuf[keypos++] = c;
        }
    } else if (keystate == GETCHAR) {
        inbuf[0] = c;
        inbuf[1] = 0;
        inlock = 0;
        keypos = 0;
    }
}

u32 IO::read(char* buf, u32 count) {
    if (count > 1) keystate = BUFFERED;
    else keystate = GETCHAR;
    asm("sti");
    inlock = 1;
    while (inlock == 1);
    asm("cli");
    strncpy(buf, inbuf, count);
    return strlen(buf);
}
