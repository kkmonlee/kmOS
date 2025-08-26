#include <os.h>
#include <keyboard.h>
#include <io.h>

Keyboard keyboard;

// US QWERTY keyboard layout
static char scancode_to_char[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',     // 0x00-0x07
    '7', '8',  '9', '0', '-', '=',   8,   9,     // 0x08-0x0F (8=backspace, 9=tab)
    'q', 'w',  'e', 'r', 't', 'y', 'u', 'i',    // 0x10-0x17
    'o', 'p',  '[', ']',  13,   0,  'a', 's',    // 0x18-0x1F (13=enter)
    'd', 'f',  'g', 'h', 'j', 'k', 'l', ';',    // 0x20-0x27
    '\'','`',   0, '\\', 'z', 'x', 'c', 'v',    // 0x28-0x2F
    'b', 'n',  'm', ',', '.', '/',   0,  '*',    // 0x30-0x37
    0,   ' ',   0,   0,   0,   0,   0,   0,      // 0x38-0x3F (space=0x39)
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x40-0x47
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x48-0x4F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x50-0x57
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x58-0x5F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x60-0x67
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x68-0x6F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x70-0x77
    0,    0,    0,   0,   0,   0,   0,   0       // 0x78-0x7F
};

// Shifted characters
static char scancode_to_char_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^',     // 0x00-0x07
    '&', '*',  '(', ')', '_', '+',   8,   9,     // 0x08-0x0F
    'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I',    // 0x10-0x17
    'O', 'P',  '{', '}',  13,   0,  'A', 'S',    // 0x18-0x1F
    'D', 'F',  'G', 'H', 'J', 'K', 'L', ':',    // 0x20-0x27
    '"', '~',   0,  '|', 'Z', 'X', 'C', 'V',    // 0x28-0x2F
    'B', 'N',  'M', '<', '>', '?',   0,  '*',    // 0x30-0x37
    0,   ' ',   0,   0,   0,   0,   0,   0,      // 0x38-0x3F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x40-0x47
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x48-0x4F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x50-0x57
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x58-0x5F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x60-0x67
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x68-0x6F
    0,    0,    0,   0,   0,   0,   0,   0,      // 0x70-0x77
    0,    0,    0,   0,   0,   0,   0,   0       // 0x78-0x7F
};

void Keyboard::init() {
    io.print("[KEYBOARD] Initializing keyboard driver\n");
    
    buffer_head = 0;
    buffer_tail = 0;
    buffer_count = 0;
    
    state.shift_pressed = false;
    state.ctrl_pressed = false;
    state.alt_pressed = false;
    state.caps_lock = false;
    
    // Clear the keyboard buffer
    while (read_status() & KEYBOARD_STATUS_OUT_BUFFER_FULL) {
        read_data();
    }
    
    io.print("[KEYBOARD] Keyboard driver initialized\n");
}

u8 Keyboard::read_data() {
    return io.inb(KEYBOARD_DATA_PORT);
}

u8 Keyboard::read_status() {
    return io.inb(KEYBOARD_STATUS_PORT);
}

void Keyboard::write_command(u8 command) {
    while (read_status() & KEYBOARD_STATUS_IN_BUFFER_FULL);
    io.outb(KEYBOARD_COMMAND_PORT, command);
}

void Keyboard::write_data(u8 data) {
    while (read_status() & KEYBOARD_STATUS_IN_BUFFER_FULL);
    io.outb(KEYBOARD_DATA_PORT, data);
}

void Keyboard::handle_interrupt() {
    u8 scancode = read_data();
    bool key_released = (scancode & 0x80) != 0;
    u8 key = scancode & 0x7F;
    
    // Update modifier key states
    update_key_state(key, !key_released);
    
    if (!key_released) {
        // Key press
        char c = scancode_to_ascii(key, state.shift_pressed || state.caps_lock);
        if (c != 0) {
            add_to_buffer(c);
        }
    }
}

void Keyboard::update_key_state(u8 scancode, bool pressed) {
    switch (scancode) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            state.shift_pressed = pressed;
            break;
        case KEY_CTRL:
            state.ctrl_pressed = pressed;
            break;
        case KEY_ALT:
            state.alt_pressed = pressed;
            break;
        case KEY_CAPS:
            if (pressed) {
                state.caps_lock = !state.caps_lock;
            }
            break;
    }
}

char Keyboard::scancode_to_ascii(u8 scancode, bool shift) {
    if (scancode >= 128) {
        return 0;
    }
    
    if (shift) {
        return scancode_to_char_shift[scancode];
    } else {
        return scancode_to_char[scancode];
    }
}

void Keyboard::add_to_buffer(char c) {
    if (buffer_count < KEYBOARD_BUFFER_SIZE - 1) {
        input_buffer[buffer_head] = c;
        buffer_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
        buffer_count++;
    }
}

char Keyboard::get_from_buffer() {
    if (buffer_count == 0) {
        return 0;
    }
    
    char c = input_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    buffer_count--;
    return c;
}

char Keyboard::get_char() {
    return get_from_buffer();
}

bool Keyboard::char_available() {
    return buffer_count > 0;
}

bool Keyboard::key_pressed(u8 scancode) {
    switch (scancode) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            return state.shift_pressed;
        case KEY_CTRL:
            return state.ctrl_pressed;
        case KEY_ALT:
            return state.alt_pressed;
        case KEY_CAPS:
            return state.caps_lock;
        default:
            return false;
    }
}

int Keyboard::read_line(char* buffer, int max_len) {
    int pos = 0;
    char c;
    
    while (pos < max_len - 1) {
        while (!char_available()) {
            // Wait for input (in real implementation, this would yield)
            asm volatile("hlt");
        }
        
        c = get_char();
        
        if (c == '\n' || c == '\r') {
            // Enter pressed
            buffer[pos] = '\0';
            io.print("\n");
            return pos;
        } else if (c == '\b' || c == 127) {
            // Backspace
            if (pos > 0) {
                pos--;
                io.print("\b \b");  // Move back, print space, move back again
            }
        } else if (c >= 32 && c < 127) {
            // Printable character
            buffer[pos] = c;
            io.print("%c", c);
            pos++;
        }
    }
    
    buffer[pos] = '\0';
    return pos;
}

void Keyboard::clear_buffer() {
    buffer_head = 0;
    buffer_tail = 0;
    buffer_count = 0;
}

// C interface functions
extern "C" {
    void keyboard_interrupt_handler() {
        keyboard.handle_interrupt();
    }
    
    void init_keyboard() {
        keyboard.init();
    }
    
    char keyboard_getchar() {
        return keyboard.get_char();
    }
    
    int keyboard_available() {
        return keyboard.char_available() ? 1 : 0;
    }
}