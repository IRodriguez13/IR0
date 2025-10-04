#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// PS/2 Controller ports
#define PS2_DATA_PORT           0x60
#define PS2_STATUS_PORT         0x64
#define PS2_COMMAND_PORT        0x64

// PS/2 Controller commands
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9
#define PS2_CMD_WRITE_PORT2     0xD4

// PS/2 Mouse commands
#define PS2_MOUSE_RESET         0xFF
#define PS2_MOUSE_RESEND        0xFE
#define PS2_MOUSE_SET_DEFAULTS  0xF6
#define PS2_MOUSE_DISABLE       0xF5
#define PS2_MOUSE_ENABLE        0xF4
#define PS2_MOUSE_SET_SAMPLE    0xF3
#define PS2_MOUSE_GET_ID        0xF2
#define PS2_MOUSE_SET_REMOTE    0xF0
#define PS2_MOUSE_SET_WRAP      0xEE
#define PS2_MOUSE_RESET_WRAP    0xEC
#define PS2_MOUSE_READ_DATA     0xEB
#define PS2_MOUSE_SET_STREAM    0xEA
#define PS2_MOUSE_STATUS        0xE9
#define PS2_MOUSE_SET_RESOLUTION 0xE8
#define PS2_MOUSE_SET_SCALING_2_1 0xE7
#define PS2_MOUSE_SET_SCALING_1_1 0xE6

// Mouse responses
#define PS2_MOUSE_ACK           0xFA
#define PS2_MOUSE_NACK          0xFE
#define PS2_MOUSE_ERROR         0xFC

// Mouse packet flags
#define PS2_MOUSE_LEFT_BUTTON   0x01
#define PS2_MOUSE_RIGHT_BUTTON  0x02
#define PS2_MOUSE_MIDDLE_BUTTON 0x04
#define PS2_MOUSE_X_SIGN        0x10
#define PS2_MOUSE_Y_SIGN        0x20
#define PS2_MOUSE_X_OVERFLOW    0x40
#define PS2_MOUSE_Y_OVERFLOW    0x80

// Mouse types
typedef enum {
    PS2_MOUSE_TYPE_STANDARD = 0x00,
    PS2_MOUSE_TYPE_WHEEL = 0x03,
    PS2_MOUSE_TYPE_5BUTTON = 0x04
} ps2_mouse_type_t;

// Mouse state structure
typedef struct {
    int32_t x;
    int32_t y;
    int8_t wheel;
    bool left_button;
    bool right_button;
    bool middle_button;
    bool button4;
    bool button5;
    bool has_wheel;
    bool has_5buttons;
    uint8_t resolution;
    uint8_t sample_rate;
    ps2_mouse_type_t type;
    bool initialized;
} ps2_mouse_state_t;

// Mouse packet structure
typedef struct {
    uint8_t flags;
    int16_t delta_x;
    int16_t delta_y;
    int8_t delta_wheel;
    uint8_t extra_buttons;
} ps2_mouse_packet_t;

// Function prototypes
bool ps2_mouse_init(void);
void ps2_mouse_shutdown(void);
bool ps2_mouse_is_available(void);
ps2_mouse_state_t* ps2_mouse_get_state(void);

// Mouse control
bool ps2_mouse_enable(void);
bool ps2_mouse_disable(void);
bool ps2_mouse_reset(void);
bool ps2_mouse_set_defaults(void);

// Configuration
bool ps2_mouse_set_sample_rate(uint8_t rate);
bool ps2_mouse_set_resolution(uint8_t resolution);
bool ps2_mouse_set_scaling_2_1(void);
bool ps2_mouse_set_scaling_1_1(void);

// Data handling
void ps2_mouse_handle_interrupt(void);
bool ps2_mouse_read_packet(ps2_mouse_packet_t *packet);
void ps2_mouse_process_packet(const ps2_mouse_packet_t *packet);

// Low-level functions
bool ps2_mouse_send_command(uint8_t command);
bool ps2_mouse_send_command_with_data(uint8_t command, uint8_t data);
uint8_t ps2_mouse_read_data(void);
bool ps2_mouse_wait_ack(void);

// Utility functions
bool ps2_controller_wait_input(void);
bool ps2_controller_wait_output(void);
void ps2_controller_write_command(uint8_t command);
void ps2_controller_write_data(uint8_t data);
uint8_t ps2_controller_read_data(void);

// Mouse type detection
ps2_mouse_type_t ps2_mouse_detect_type(void);
bool ps2_mouse_enable_wheel(void);
bool ps2_mouse_enable_5buttons(void);

#endif // PS2_MOUSE_H