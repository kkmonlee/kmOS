#ifndef __KBD__
#define __KBD__

#include <api/dev/tty.h>

char kbdmap_default[] = {
    (char)KEY_ESCAPE, (char)KEY_ESCAPE, (char)KEY_ESCAPE, (char)KEY_ESCAPE, // esc
    '1', '!', '1', '1',
    '2', '@', '2', '2',
    '3', '#', '3', '3',
    '4', '$', '4', '4',
    '5', '%', '5', '5',
    '6', ':', '6', '6',
    '7', '&', '7', '7',
    '8', '*', '8', '8',
    '9', '(', '9', '9',
    '0', ')', '0', '0',
    '-', '_', '-', '-',
    '=', '+', '=', '=',
    0x08, 0x08, 0x7F, 0x08, // backspace
    0x09, 0x09, 0x09, 0x09, // tab
    'a', 'A', 'a', 'a',
    'z', 'Z', 'z', 'z',
    'e', 'E', 'e', 'e',
    'r', 'R', 'r', 'r',
    't', 'T', 't', 't',
    'y', 'Y', 'y', 'y',
    'u', 'U', 'u', 'u',
    'i', 'I', 'i', 'i',
    'o', 'O', 'o', 'o',
    'p', 'P', 'p', 'p',
    '^', '"', '^', '^',
    '$', '#', ' ', '$',
    0x0A, 0x0A, 0x0A, 0x0A, // enter
    (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, // ctrl
    'q', 'Q', 'q', 'q',
    's', 'S', 's', 's',
    'd', 'D', 'd', 'd',
    'f', 'F', 'f', 'f',
    'g', 'G', 'g', 'g',
    'h', 'H', 'h', 'h',
    'j', 'J', 'j', 'j',
    'k', 'K', 'k', 'k',
    'l', 'L', 'l', 'l',
    'm', 'M', 'm', 'm',
    0x27, 0x22, 0x27, 0x27, // '"
    '*', '~', '`', '`',     // `~
    (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, // Lshift
    '<', '>', '\\', '\\',
    'w', 'W', 'w', 'w',
    'x', 'X', 'x', 'x',
    'c', 'C', 'c', 'c',
    'v', 'V', 'v', 'v',
    'b', 'B', 'b', 'b',
    'n', 'N', 'n', 'n',
    ',', '?', ',', ',',
    0x2C, 0x3C, 0x2C, 0x2C, // ,<
    0x2E, 0x3E, 0x2E, 0x2E, // .>
    0x2F, 0x3F, 0x2F, 0x2F, // /?
    (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, // Rshift
    (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, // reserved
    (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF,
    ' ', ' ', ' ', ' ', // space
    (char)KEY_F1, (char)0xFF, (char)0xFF, (char)0xFF,
    (char)KEY_F2, (char)0xFF, (char)0xFF, (char)0xFF,
    (char)KEY_F3, (char)0xFF, (char)0xFF, (char)0xFF,
    // remaining keys as placeholders
    (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF};

char *kbdmap = kbdmap_default;

#endif
