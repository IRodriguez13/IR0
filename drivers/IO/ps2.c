#include "ps2.h"
#include <print.h>
#include <panic/panic.h>
#include <string.h>

// Global variables
uint8_t keyboard_buffer[KEYBOARD_BUFFER_SIZE];
int keyboard_buffer_head = 0;
int keyboard_buffer_tail = 0;
bool keyboard_shift_pressed = false;
bool keyboard_ctrl_pressed = false;
bool keyboard_alt_pressed = false;

// ASCII conversion tables
static const char scancode_to_ascii_normal[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static const char scancode_to_ascii_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

// I/O functions
static inline void outb(uint16_t port, uint8_t value) 
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// PS/2 Controller functions
void ps2_init(void) {
    print("Initializing PS/2 controller...\n");
    
    // Disable both ports
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT1);
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT2);
    
    // Flush output buffer
    inb(PS2_DATA_PORT);
    
    // Set configuration byte
    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    uint8_t config = inb(PS2_DATA_PORT);
    config &= ~(1 << 0); // Disable IRQ1
    config &= ~(1 << 1); // Disable IRQ12
    config |= (1 << 6);  // Enable translation
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    outb(PS2_DATA_PORT, config);
    
    // Test controller
    outb(PS2_COMMAND_PORT, PS2_CMD_TEST_CONTROLLER);
    if (inb(PS2_DATA_PORT) != 0x55) {
        print_error("PS/2 controller test failed\n");
        return;
    }
    
    // Enable port 1
    outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_PORT1);
    
    // Test port 1
    outb(PS2_COMMAND_PORT, PS2_CMD_TEST_PORT1);
    if (inb(PS2_DATA_PORT) != 0x00) {
        print_error("PS/2 port 1 test failed\n");
        return;
    }
    
    // Set configuration byte again to enable IRQ1
    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    config = inb(PS2_DATA_PORT);
    config |= (1 << 0);  // Enable IRQ1
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    outb(PS2_DATA_PORT, config);
    
    print_success("PS/2 controller initialized\n");
}

bool ps2_send_command(uint8_t command) {
    // Wait for input buffer to be empty
    for (int i = 0; i < 1000; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            break;
        }
    }
    
    outb(PS2_COMMAND_PORT, command);
    return true;
}

uint8_t ps2_read_data(void) {
    // Wait for output buffer to be full
    for (int i = 0; i < 1000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            break;
        }
    }
    
    return inb(PS2_DATA_PORT);
}

bool ps2_wait_for_output(void) {
    for (int i = 0; i < 1000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return true;
        }
    }
    return false;
}

bool ps2_wait_for_input(void) {
    for (int i = 0; i < 1000; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return true;
        }
    }
    return false;
}

// Keyboard functions
void ps2_keyboard_init(void) {
    print("Initializing PS/2 keyboard...\n");
    
    // Reset keyboard
    if (!ps2_keyboard_send_command(PS2_DEV_RESET)) {
        print_error("Keyboard reset failed\n");
        return;
    }
    
    // Wait for ACK
    uint8_t response = ps2_read_data();
    if (response != 0xFA) {
        print_error("Keyboard reset ACK failed\n");
        return;
    }
    
    // Wait for reset complete
    response = ps2_read_data();
    if (response != 0xAA) {
        print_error("Keyboard reset complete failed\n");
        return;
    }
    
    // Enable scanning
    if (!ps2_keyboard_send_command(PS2_DEV_ENABLE_SCAN)) {
        print_error("Keyboard enable scan failed\n");
        return;
    }
    
    response = ps2_read_data();
    if (response != 0xFA) {
        print_error("Keyboard enable scan ACK failed\n");
        return;
    }
    
    print_success("PS/2 keyboard initialized\n");
}

bool ps2_keyboard_send_command(uint8_t command) {
    return ps2_send_command(command);
}

uint8_t ps2_keyboard_read_scancode(void) {
    if (ps2_wait_for_output()) {
        return inb(PS2_DATA_PORT);
    }
    return 0;
}

char ps2_scancode_to_ascii(uint8_t scancode) {
    if (scancode >= sizeof(scancode_to_ascii_normal)) {
        return 0;
    }
    
    if (keyboard_shift_pressed) {
        return scancode_to_ascii_shift[scancode];
    } else {
        return scancode_to_ascii_normal[scancode];
    }
}

bool ps2_is_key_pressed(uint8_t scancode) {
    // Check if key is in buffer
    int i = keyboard_buffer_head;
    while (i != keyboard_buffer_tail) {
        if (keyboard_buffer[i] == scancode) {
            return true;
        }
        i = (i + 1) % KEYBOARD_BUFFER_SIZE;
    }
    return false;
}

bool ps2_is_shift_pressed(void) {
    return keyboard_shift_pressed;
}

bool ps2_is_ctrl_pressed(void) {
    return keyboard_ctrl_pressed;
}

bool ps2_is_alt_pressed(void) {
    return keyboard_alt_pressed;
}

void ps2_keyboard_handler(void) {
    uint8_t scancode = ps2_keyboard_read_scancode();
    
    if (scancode == 0) {
        return;
    }
    
    // Handle special keys
    if (scancode == KEY_LSHIFT || scancode == KEY_RSHIFT) {
        keyboard_shift_pressed = true;
        return;
    }
    
    if (scancode == (KEY_LSHIFT | KEY_RELEASE) || scancode == (KEY_RSHIFT | KEY_RELEASE)) {
        keyboard_shift_pressed = false;
        return;
    }
    
    if (scancode == KEY_LCTRL) {
        keyboard_ctrl_pressed = true;
        return;
    }
    
    if (scancode == (KEY_LCTRL | KEY_RELEASE)) {
        keyboard_ctrl_pressed = false;
        return;
    }
    
    if (scancode == KEY_LALT) {
        keyboard_alt_pressed = true;
        return;
    }
    
    if (scancode == (KEY_LALT | KEY_RELEASE)) {
        keyboard_alt_pressed = false;
        return;
    }
    
    // Handle key release
    if (scancode & KEY_RELEASE) {
        return;
    }
    
    // Add to buffer
    int next_tail = (keyboard_buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_tail != keyboard_buffer_head) {
        keyboard_buffer[keyboard_buffer_tail] = scancode;
        keyboard_buffer_tail = next_tail;
    }
}

// Public interface functions
char ps2_get_char(void) {
    if (keyboard_buffer_head == keyboard_buffer_tail) {
        return 0;
    }
    
    uint8_t scancode = keyboard_buffer[keyboard_buffer_head];
    keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    
    return ps2_scancode_to_ascii(scancode);
}

bool ps2_has_char(void) {
    return keyboard_buffer_head != keyboard_buffer_tail;
}

void ps2_flush_buffer(void) {
    keyboard_buffer_head = keyboard_buffer_tail;
}
