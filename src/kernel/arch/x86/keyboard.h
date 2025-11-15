#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <runtime/types.h>


#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64
#define KEYBOARD_COMMAND_PORT 0x64


#define KEYBOARD_STATUS_OUT_BUFFER_FULL  0x01
#define KEYBOARD_STATUS_IN_BUFFER_FULL   0x02
#define KEYBOARD_STATUS_SYSTEM_FLAG      0x04
#define KEYBOARD_STATUS_COMMAND_DATA     0x08
#define KEYBOARD_STATUS_KEYBOARD_LOCK    0x10
#define KEYBOARD_STATUS_AUX_OUT_BUFFER   0x20
#define KEYBOARD_STATUS_TIMEOUT          0x40
#define KEYBOARD_STATUS_PARITY_ERROR     0x80


#define KEY_ESC       0x01
#define KEY_BACKSPACE 0x0E
#define KEY_TAB       0x0F
#define KEY_ENTER     0x1C
#define KEY_CTRL      0x1D
#define KEY_LSHIFT    0x2A
#define KEY_RSHIFT    0x36
#define KEY_ALT       0x38
#define KEY_CAPS      0x3A
#define KEY_F1        0x3B
#define KEY_F2        0x3C
#define KEY_F3        0x3D
#define KEY_F4        0x3E
#define KEY_F5        0x3F
#define KEY_F6        0x40
#define KEY_F7        0x41
#define KEY_F8        0x42
#define KEY_F9        0x43
#define KEY_F10       0x44

#define KEY_UP        0x48
#define KEY_DOWN      0x50
#define KEY_LEFT      0x4B
#define KEY_RIGHT     0x4D

#define KEYBOARD_BUFFER_SIZE 256

struct keyboard_state {
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    bool caps_lock;
};

class Keyboard {
public:
    void init();
    char get_char();
    bool char_available();
    bool key_pressed(u8 scancode);
    void handle_interrupt();
    

    int read_line(char* buffer, int max_len);
    void clear_buffer();

private:
    char input_buffer[KEYBOARD_BUFFER_SIZE];
    int buffer_head;
    int buffer_tail;
    int buffer_count;
    
    struct keyboard_state state;
    
    u8 read_data();
    u8 read_status();
    void write_command(u8 command);
    void write_data(u8 data);
    
    char scancode_to_ascii(u8 scancode, bool shift);
    void update_key_state(u8 scancode, bool pressed);
    void add_to_buffer(char c);
    char get_from_buffer();
};

extern Keyboard keyboard;


extern "C" {
    void keyboard_interrupt_handler();
    void init_keyboard();
    char keyboard_getchar();
    int keyboard_available();
}

#endif