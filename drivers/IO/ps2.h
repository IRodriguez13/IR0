#pragma once

#include <stdint.h>
#include <stdbool.h>

// PS/2 Controller ports
#define PS2_DATA_PORT        0x60
#define PS2_COMMAND_PORT     0x64
#define PS2_STATUS_PORT      0x64

// PS/2 Commands
#define PS2_CMD_READ_CONFIG  0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_DISABLE_PORT2 0xA7
#define PS2_CMD_ENABLE_PORT2  0xA8
#define PS2_CMD_TEST_PORT2    0xA9
#define PS2_CMD_TEST_CONTROLLER 0xAA
#define PS2_CMD_TEST_PORT1    0xAB
#define PS2_CMD_DISABLE_PORT1 0xAD
#define PS2_CMD_ENABLE_PORT1  0xAE
#define PS2_CMD_READ_OUTPUT   0xD0
#define PS2_CMD_WRITE_OUTPUT  0xD1

// PS/2 Device commands
#define PS2_DEV_RESET        0xFF
#define PS2_DEV_IDENTIFY     0xF2
#define PS2_DEV_ENABLE_SCAN  0xF4
#define PS2_DEV_DISABLE_SCAN 0xF5
#define PS2_DEV_SET_DEFAULTS 0xF6

// PS/2 Status register bits
#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02
#define PS2_STATUS_SYSTEM_FLAG  0x04
#define PS2_STATUS_COMMAND_DATA 0x08
#define PS2_STATUS_TIMEOUT      0x40
#define PS2_STATUS_PARITY_ERROR 0x80

// Keyboard scan codes (set 1)
#define KEY_ESC          0x01
#define KEY_1            0x02
#define KEY_2            0x03
#define KEY_3            0x04
#define KEY_4            0x05
#define KEY_5            0x06
#define KEY_6            0x07
#define KEY_7            0x08
#define KEY_8            0x09
#define KEY_9            0x0A
#define KEY_0            0x0B
#define KEY_MINUS        0x0C
#define KEY_EQUALS       0x0D
#define KEY_BACKSPACE    0x0E
#define KEY_TAB          0x0F
#define KEY_Q            0x10
#define KEY_W            0x11
#define KEY_E            0x12
#define KEY_R            0x13
#define KEY_T            0x14
#define KEY_Y            0x15
#define KEY_U            0x16
#define KEY_I            0x17
#define KEY_O            0x18
#define KEY_P            0x19
#define KEY_LBRACKET     0x1A
#define KEY_RBRACKET     0x1B
#define KEY_ENTER        0x1C
#define KEY_LCTRL        0x1D
#define KEY_A            0x1E
#define KEY_S            0x1F
#define KEY_D            0x20
#define KEY_F            0x21
#define KEY_G            0x22
#define KEY_H            0x23
#define KEY_J            0x24
#define KEY_K            0x25
#define KEY_L            0x26
#define KEY_SEMICOLON    0x27
#define KEY_QUOTE        0x28
#define KEY_BACKTICK     0x29
#define KEY_LSHIFT       0x2A
#define KEY_BACKSLASH    0x2B
#define KEY_Z            0x2C
#define KEY_X            0x2D
#define KEY_C            0x2E
#define KEY_V            0x2F
#define KEY_B            0x30
#define KEY_N            0x31
#define KEY_M            0x32
#define KEY_COMMA        0x33
#define KEY_PERIOD       0x34
#define KEY_SLASH        0x35
#define KEY_RSHIFT       0x36
#define KEY_KP_MULTIPLY  0x37
#define KEY_LALT         0x38
#define KEY_SPACE        0x39
#define KEY_CAPSLOCK     0x3A
#define KEY_F1           0x3B
#define KEY_F2           0x3C
#define KEY_F3           0x3D
#define KEY_F4           0x3E
#define KEY_F5           0x3F
#define KEY_F6           0x40
#define KEY_F7           0x41
#define KEY_F8           0x42
#define KEY_F9           0x43
#define KEY_F10          0x44
#define KEY_NUMLOCK      0x45
#define KEY_SCROLLLOCK   0x46
#define KEY_KP_7         0x47
#define KEY_KP_8         0x48
#define KEY_KP_9         0x49
#define KEY_KP_MINUS     0x4A
#define KEY_KP_4         0x4B
#define KEY_KP_5         0x4C
#define KEY_KP_6         0x4D
#define KEY_KP_PLUS      0x4E
#define KEY_KP_1         0x4F
#define KEY_KP_2         0x50
#define KEY_KP_3         0x51
#define KEY_KP_0         0x52
#define KEY_KP_PERIOD    0x53

// Special keys
#define KEY_EXTENDED     0xE0
#define KEY_RELEASE      0x80

// Keyboard buffer
#define KEYBOARD_BUFFER_SIZE 256

// Function prototypes
void ps2_init(void);
bool ps2_send_command(uint8_t command);
uint8_t ps2_read_data(void);
bool ps2_wait_for_output(void);
bool ps2_wait_for_input(void);
void ps2_keyboard_init(void);
bool ps2_keyboard_send_command(uint8_t command);
uint8_t ps2_keyboard_read_scancode(void);
char ps2_scancode_to_ascii(uint8_t scancode);
bool ps2_is_key_pressed(uint8_t scancode);
bool ps2_is_shift_pressed(void);
bool ps2_is_ctrl_pressed(void);
bool ps2_is_alt_pressed(void);
void ps2_keyboard_handler(void);

// Public interface functions
char ps2_get_char(void);
bool ps2_has_char(void);
void ps2_flush_buffer(void);

// Global variables
extern uint8_t keyboard_buffer[KEYBOARD_BUFFER_SIZE];
extern int keyboard_buffer_head;
extern int keyboard_buffer_tail;
extern bool keyboard_shift_pressed;
extern bool keyboard_ctrl_pressed;
extern bool keyboard_alt_pressed;

