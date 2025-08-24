#ifndef IO_H
#define IO_H

#include <runtime/types.h>

#define RAMSCREEN 0xB8000
#define SIZESCREEN 0xFA0
#define SCREENLIM 0xB8FA0

class IO {
public:
    IO();
    IO(u32 flag);

    enum Colour {
        Black = 0,
        Blue = 1,
        Green = 2,
        Cyan = 3,
        Red = 4,
        Magenta = 5,
        Orange = 6,
        LightGrey = 7,
        DarkGrey = 8,
        LightBlue = 9,
        LightGreen = 10,
        LightCyan = 11,
        LightRed = 12,
        LightMagenta = 13,
        Yellow = 14,
        White = 15
    };

    void outb(u32 ad, u8 v);
    void outw(u32 ad, u16);
    void outl(u32 ad, u32 v);

    u8 inb(u32 ad);
    u16 inw(u32 ad);
    u32 inl(u32 ad);

    void putctty(char c);

    u32 read(char* buf, u32 count);
    void putc(char c);
    void setColor(char fcol, char bcol);
    void setXY(char xc, char yc);
    void clear();
    void print(const char *s, ...);

    u32 getX();
    u32 getY();

    void switchtty();

    /* x86 functions */
    void scrollup(unsigned int n);
    void save_screen();
    void load_screen();

    enum ConsoleType {
        BUFFERED,
        GETCHAR
    };

    static IO* current_io;
    static IO* last_io;

private:
    /* x86 private attribs */
    char* real_screen;
    char screen[SIZESCREEN];

    char inbuf[512];
    int keypos;
    int inlock;
    int keystate;

    char fcolor;
    char bcolor;
    char x;
    char yl;
    char kattr;
    static char* vidmem;
};

extern IO io;

#endif
