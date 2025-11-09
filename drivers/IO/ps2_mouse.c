#include "ps2_mouse.h"
#include <ir0/vga.h>
#include <stddef.h>
#include <arch/common/arch_interface.h>

// Global mouse state
static ps2_mouse_state_t mouse_state = {0};
static uint8_t packet_buffer[4];
static uint8_t packet_index = 0;
static uint8_t expected_packet_size = 3;

bool ps2_mouse_init(void)
{
    // Initialize mouse state
    mouse_state.x = 0;
    mouse_state.y = 0;
    mouse_state.wheel = 0;
    mouse_state.left_button = false;
    mouse_state.right_button = false;
    mouse_state.middle_button = false;
    mouse_state.button4 = false;
    mouse_state.button5 = false;
    mouse_state.has_wheel = false;
    mouse_state.has_5buttons = false;
    mouse_state.resolution = 4;    // Default resolution
    mouse_state.sample_rate = 100; // Default sample rate
    mouse_state.type = PS2_MOUSE_TYPE_STANDARD;
    mouse_state.initialized = false;

    // Enable auxiliary device (mouse)
    ps2_controller_write_command(PS2_CMD_ENABLE_PORT2);

    // Test if mouse port is working
    ps2_controller_write_command(PS2_CMD_TEST_PORT2);
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    uint8_t test_result = ps2_controller_read_data();
    if (test_result != 0x00)
    {
        // Mouse port test failed
        return false;
    }

    // Reset mouse
    if (!ps2_mouse_reset())
    {
        return false;
    }

    // Detect mouse type and capabilities
    mouse_state.type = ps2_mouse_detect_type();

    // Enable wheel if supported
    if (mouse_state.type >= PS2_MOUSE_TYPE_WHEEL)
    {
        mouse_state.has_wheel = true;
        expected_packet_size = 4;
    }

    // Enable 5 buttons if supported
    if (mouse_state.type == PS2_MOUSE_TYPE_5BUTTON)
    {
        mouse_state.has_5buttons = true;
    }

    // Set default configuration
    ps2_mouse_set_defaults();
    ps2_mouse_set_sample_rate(100);
    ps2_mouse_set_resolution(4);

    // Enable mouse
    if (!ps2_mouse_enable())
    {
        return false;
    }

    mouse_state.initialized = true;
    return true;
}

void ps2_mouse_shutdown(void)
{
    if (!mouse_state.initialized)
    {
        return;
    }

    // Disable mouse
    ps2_mouse_disable();

    // Disable auxiliary device
    ps2_controller_write_command(PS2_CMD_DISABLE_PORT2);

    mouse_state.initialized = false;
}

bool ps2_mouse_is_available(void)
{
    return mouse_state.initialized;
}

ps2_mouse_state_t *ps2_mouse_get_state(void)
{
    return &mouse_state;
}

bool ps2_mouse_enable(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_ENABLE);
}

bool ps2_mouse_disable(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_DISABLE);
}

bool ps2_mouse_reset(void)
{
    if (!ps2_mouse_send_command(PS2_MOUSE_RESET))
    {
        return false;
    }

    // Wait for self-test result (should be 0xAA)
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    uint8_t self_test = ps2_controller_read_data();
    if (self_test != 0xAA)
    {
        return false;
    }

    // Wait for mouse ID (should be 0x00)
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    uint8_t mouse_id = ps2_controller_read_data();
    if (mouse_id != 0x00)
    {
        return false;
    }

    return true;
}

bool ps2_mouse_set_defaults(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_SET_DEFAULTS);
}

bool ps2_mouse_set_sample_rate(uint8_t rate)
{
    if (ps2_mouse_send_command_with_data(PS2_MOUSE_SET_SAMPLE, rate))
    {
        mouse_state.sample_rate = rate;
        return true;
    }
    return false;
}

bool ps2_mouse_set_resolution(uint8_t resolution)
{
    if (ps2_mouse_send_command_with_data(PS2_MOUSE_SET_RESOLUTION, resolution))
    {
        mouse_state.resolution = resolution;
        return true;
    }
    return false;
}

bool ps2_mouse_set_scaling_2_1(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_SET_SCALING_2_1);
}

bool ps2_mouse_set_scaling_1_1(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_SET_SCALING_1_1);
}

void ps2_mouse_handle_interrupt(void)
{
    if (!mouse_state.initialized)
    {
        return;
    }

    // Read data from controller
    uint8_t data = ps2_controller_read_data();

    // Store in packet buffer
    packet_buffer[packet_index] = data;
    packet_index++;

    // Check if we have a complete packet
    if (packet_index >= expected_packet_size)
    {
        ps2_mouse_packet_t packet;

        // Parse standard 3-byte packet
        packet.flags = packet_buffer[0];
        packet.delta_x = packet_buffer[1];
        packet.delta_y = packet_buffer[2];
        packet.delta_wheel = 0;
        packet.extra_buttons = 0;

        // Handle sign extension for X movement
        if (packet.flags & PS2_MOUSE_X_SIGN)
        {
            packet.delta_x |= 0xFF00;
        }

        // Handle sign extension for Y movement
        if (packet.flags & PS2_MOUSE_Y_SIGN)
        {
            packet.delta_y |= 0xFF00;
        }

        // Parse wheel data if available
        if (expected_packet_size == 4 && mouse_state.has_wheel)
        {
            packet.delta_wheel = (int8_t)(packet_buffer[3] & 0x0F);
            if (packet.delta_wheel & 0x08)
            {
                packet.delta_wheel |= 0xF0; // Sign extend
            }

            // Parse extra buttons if available
            if (mouse_state.has_5buttons)
            {
                packet.extra_buttons = (packet_buffer[3] >> 4) & 0x03;
            }
        }

        // Process the packet
        ps2_mouse_process_packet(&packet);

        // Reset packet buffer
        packet_index = 0;
    }
}

bool ps2_mouse_read_packet(ps2_mouse_packet_t *packet)
{
    if (!packet || !mouse_state.initialized)
    {
        return false;
    }

    // This would typically be called from interrupt handler
    // For now, just return current state
    packet->flags = 0;
    if (mouse_state.left_button)
        packet->flags |= PS2_MOUSE_LEFT_BUTTON;
    if (mouse_state.right_button)
        packet->flags |= PS2_MOUSE_RIGHT_BUTTON;
    if (mouse_state.middle_button)
        packet->flags |= PS2_MOUSE_MIDDLE_BUTTON;

    packet->delta_x = 0; // Would be calculated from movement
    packet->delta_y = 0;
    packet->delta_wheel = mouse_state.wheel;
    packet->extra_buttons = 0;
    if (mouse_state.button4)
        packet->extra_buttons |= 0x01;
    if (mouse_state.button5)
        packet->extra_buttons |= 0x02;

    return true;
}

void ps2_mouse_process_packet(const ps2_mouse_packet_t *packet)
{
    if (!packet)
    {
        return;
    }

    // Update button states
    mouse_state.left_button = (packet->flags & PS2_MOUSE_LEFT_BUTTON) != 0;
    mouse_state.right_button = (packet->flags & PS2_MOUSE_RIGHT_BUTTON) != 0;
    mouse_state.middle_button = (packet->flags & PS2_MOUSE_MIDDLE_BUTTON) != 0;

    // Update position (with bounds checking)
    mouse_state.x += packet->delta_x;
    mouse_state.y -= packet->delta_y; // Y is inverted in PS/2

    // Clamp coordinates to reasonable bounds
    if (mouse_state.x < 0)
        mouse_state.x = 0;
    if (mouse_state.y < 0)
        mouse_state.y = 0;
    if (mouse_state.x > 1023)
        mouse_state.x = 1023;
    if (mouse_state.y > 767)
        mouse_state.y = 767;

    // Update wheel
    if (mouse_state.has_wheel)
    {
        mouse_state.wheel = packet->delta_wheel;
    }

    // Update extra buttons
    if (mouse_state.has_5buttons)
    {
        mouse_state.button4 = (packet->extra_buttons & 0x01) != 0;
        mouse_state.button5 = (packet->extra_buttons & 0x02) != 0;
    }
}

bool ps2_mouse_send_command(uint8_t command)
{
    // Send command to mouse via controller
    ps2_controller_write_command(PS2_CMD_WRITE_PORT2);
    if (!ps2_controller_wait_input())
    {
        return false;
    }

    ps2_controller_write_data(command);

    return ps2_mouse_wait_ack();
}

bool ps2_mouse_send_command_with_data(uint8_t command, uint8_t data)
{
    if (!ps2_mouse_send_command(command))
    {
        return false;
    }

    // Send data byte
    ps2_controller_write_command(PS2_CMD_WRITE_PORT2);
    if (!ps2_controller_wait_input())
    {
        return false;
    }

    ps2_controller_write_data(data);

    return ps2_mouse_wait_ack();
}

uint8_t ps2_mouse_read_data(void)
{
    return ps2_controller_read_data();
}

bool ps2_mouse_wait_ack(void)
{
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    uint8_t response = ps2_controller_read_data();
    return response == PS2_MOUSE_ACK;
}

bool ps2_controller_wait_input(void)
{
    int timeout = 1000;
    while (timeout-- > 0)
    {
        if ((inb(PS2_STATUS_PORT) & 0x02) == 0)
        {
            return true;
        }
        for (volatile int i = 0; i < 100; i++)
            ;
    }
    return false;
}

bool ps2_controller_wait_output(void)
{
    int timeout = 1000;
    while (timeout-- > 0)
    {
        if (inb(PS2_STATUS_PORT) & 0x01)
        {
            return true;
        }
        for (volatile int i = 0; i < 100; i++)
            ;
    }
    return false;
}

void ps2_controller_write_command(uint8_t command)
{
    ps2_controller_wait_input();
    outb(PS2_COMMAND_PORT, command);
}

void ps2_controller_write_data(uint8_t data)
{
    ps2_controller_wait_input();
    outb(PS2_DATA_PORT, data);
}

uint8_t ps2_controller_read_data(void)
{
    ps2_controller_wait_output();
    return inb(PS2_DATA_PORT);
}

ps2_mouse_type_t ps2_mouse_detect_type(void)
{
    // Try to enable wheel mouse mode
    if (ps2_mouse_enable_wheel())
    {
        // Check if 5-button mode is available
        if (ps2_mouse_enable_5buttons())
        {
            return PS2_MOUSE_TYPE_5BUTTON;
        }
        return PS2_MOUSE_TYPE_WHEEL;
    }

    return PS2_MOUSE_TYPE_STANDARD;
}

bool ps2_mouse_enable_wheel(void)
{
    // Magic sequence to enable wheel: 200, 100, 80
    if (!ps2_mouse_set_sample_rate(200))
        return false;
    if (!ps2_mouse_set_sample_rate(100))
        return false;
    if (!ps2_mouse_set_sample_rate(80))
        return false;

    // Get device ID
    if (!ps2_mouse_send_command(PS2_MOUSE_GET_ID))
    {
        return false;
    }

    if (!ps2_controller_wait_output())
    {
        return false;
    }

    uint8_t id = ps2_controller_read_data();
    return id == 0x03; // Wheel mouse ID
}

bool ps2_mouse_enable_5buttons(void)
{
    // Magic sequence to enable 5-button mode: 200, 200, 80
    if (!ps2_mouse_set_sample_rate(200))
        return false;
    if (!ps2_mouse_set_sample_rate(200))
        return false;
    if (!ps2_mouse_set_sample_rate(80))
        return false;

    // Get device ID
    if (!ps2_mouse_send_command(PS2_MOUSE_GET_ID))
    {
        return false;
    }

    if (!ps2_controller_wait_output())
    {
        return false;
    }

    uint8_t id = ps2_controller_read_data();
    return id == 0x04; // 5-button mouse ID
}