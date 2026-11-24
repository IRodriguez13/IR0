/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2_mouse.c
 * Description: PS/2 Mouse driver with IntelliMouse extension support
 */

#include "ps2_mouse.h"
#include <config.h>
#include <ir0/vga.h>
#include <stddef.h>
#include <ir0/driver.h>
#include <ir0/input.h>
#include <ir0/ktm/klog.h>
#include <ir0/ps2_mouse_pkt.h>
#include <ir0/console.h>
#include <asm/io.h>

static void ps2_mouse_shutdown(void);
static bool ps2_mouse_enable(void);
static bool ps2_mouse_disable(void);
static bool ps2_mouse_reset(void);
static bool ps2_mouse_set_defaults(void);
static bool ps2_mouse_set_resolution(uint8_t resolution);
static bool ps2_mouse_set_scaling_2_1(void);
static void ps2_mouse_process_packet(const ps2_mouse_packet_t *packet);
static bool ps2_mouse_send_command(uint8_t command);
static bool ps2_mouse_send_command_with_data(uint8_t command, uint8_t data);
static bool ps2_mouse_wait_ack(void);
static bool ps2_controller_wait_input(void);
static bool ps2_controller_wait_output(void);
static void ps2_controller_write_command(uint8_t command);
static void ps2_controller_write_data(uint8_t data);
static uint8_t ps2_controller_read_data(void);
static ps2_mouse_type_t ps2_mouse_detect_type(void);
static bool ps2_mouse_enable_wheel(void);
static bool ps2_mouse_enable_5buttons(void);

/* Global mouse state */
static ps2_mouse_state_t mouse_state = {0};
/* The PS/2 mouse behind i8042 port 2 (Linux psmouse). */
static struct input_dev *mouse_dev;
static struct ps2_mouse_pkt_state pkt_state;
static uint8_t expected_packet_size = 3;

/* Internal hardware initialization function */
/* Linux psmouse names and ids for the protocol the mouse answered. */
static int32_t ps2_mouse_register_input(void)
{
    struct input_dev *dev;

    if (mouse_dev)
        return 0;
    dev = input_allocate_device();
    if (!dev)
        return -1;
    dev->phys = "isa0060/serio1/input0";
    dev->id.bustype = BUS_I8042;
    dev->id.vendor = 0x0002;
    __set_bit(EV_KEY, dev->evbit);
    __set_bit(EV_REL, dev->evbit);
    __set_bit(BTN_LEFT, dev->keybit);
    __set_bit(BTN_RIGHT, dev->keybit);
    __set_bit(BTN_MIDDLE, dev->keybit);
    __set_bit(REL_X, dev->relbit);
    __set_bit(REL_Y, dev->relbit);
    __set_bit(INPUT_PROP_POINTER, dev->propbit);
    if (mouse_state.has_5buttons)
    {
        dev->name = "ImExPS/2 Generic Explorer Mouse";
        dev->id.product = 0x0004;
        __set_bit(BTN_SIDE, dev->keybit);
        __set_bit(BTN_EXTRA, dev->keybit);
        __set_bit(REL_WHEEL, dev->relbit);
    }
    else if (mouse_state.has_wheel)
    {
        dev->name = "ImPS/2 Generic Wheel Mouse";
        dev->id.product = 0x0003;
        __set_bit(REL_WHEEL, dev->relbit);
    }
    else
    {
        dev->name = "PS/2 Generic Mouse";
        dev->id.product = 0x0001;
    }
    if (input_register_device(dev) != 0)
    {
        input_free_device(dev);
        return -1;
    }
    mouse_dev = dev;
    return 0;
}

static int32_t ps2_mouse_hw_init(void)
{
    /* Initialize mouse state */
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
    mouse_state.resolution = PS2_MOUSE_DEFAULT_RESOLUTION;
    mouse_state.sample_rate = PS2_MOUSE_DEFAULT_SAMPLE_RATE;
    mouse_state.type = PS2_MOUSE_TYPE_STANDARD;
    mouse_state.initialized = false;

    /* Enable auxiliary device (mouse) */
    ps2_controller_write_command(PS2_CMD_ENABLE_PORT2);

    /* Test if mouse port is working */
    ps2_controller_write_command(PS2_CMD_TEST_PORT2);
    if (!ps2_controller_wait_output())
    {
        return -1;
    }

    if (ps2_controller_read_data() != 0x00) /* Port test OK response */
    {
        /* Mouse port test failed */
        klog_error("PS2_MOUSE", "Mouse port test failed");
        return -1;
    }

    /* Reset mouse */
    if (!ps2_mouse_reset())
    {
        return -1;
    }

    /* Detect mouse type and capabilities */
    mouse_state.type = ps2_mouse_detect_type();

    /* Enable wheel if supported */
    if (mouse_state.type >= PS2_MOUSE_TYPE_WHEEL)
    {
        mouse_state.has_wheel = true;
        expected_packet_size = 4;
        ps2_mouse_pkt_reset(&pkt_state, expected_packet_size);
    }

    /* Enable 5 buttons if supported */
    if (mouse_state.type == PS2_MOUSE_TYPE_5BUTTON)
    {
        mouse_state.has_5buttons = true;
    }

    /* Set default configuration */
    ps2_mouse_set_defaults();
    ps2_mouse_set_sample_rate(PS2_MOUSE_DEFAULT_SAMPLE_RATE);
    ps2_mouse_set_resolution(PS2_MOUSE_DEFAULT_RESOLUTION);
    /* Linux PS/2 scaling 2:1 — faster pointer without userspace patches. */
    (void)ps2_mouse_set_scaling_2_1();

    /* Enable mouse */
    if (!ps2_mouse_enable())
    {
        return -1;
    }

    /* Enable IRQ12 (AUX) on the i8042 config byte. */
    {
	uint8_t config;

	ps2_controller_write_command(PS2_CMD_READ_CONFIG);
	if (ps2_controller_wait_output())
	{
		config = ps2_controller_read_data();
		config |= PS2_CFG_INT2;
		config &= (uint8_t)~PS2_CFG_CLK2; /* clear AUX clock disable */
		ps2_controller_write_command(PS2_CMD_WRITE_CONFIG);
		ps2_controller_write_data(config);
	}
    }

    mouse_state.initialized = true;
    ps2_mouse_pkt_reset(&pkt_state, expected_packet_size);
    return ps2_mouse_register_input();
}

/* Driver registration structures */
static ir0_driver_ops_t mouse_ops = {
    .init = ps2_mouse_hw_init,
    .shutdown = ps2_mouse_shutdown
};

static ir0_driver_info_t mouse_info = {
    .name = "PS/2 Mouse",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "PS/2 Mouse Driver with IntelliMouse support",
    .language = IR0_DRIVER_LANG_C
};

/**
 * ps2_mouse_init - register PS/2 mouse driver
 */

bool ps2_mouse_init(void)
{
    klog_info("PS2_MOUSE", "Registering PS/2 Mouse driver...");
    ir0_driver_t* drv = ir0_register_driver(&mouse_info, &mouse_ops);
    return drv != NULL;
}

static void ps2_mouse_shutdown(void)
{
    if (!mouse_state.initialized)
    {
        return;
    }

    /* Disable mouse */
    ps2_mouse_disable();

    /* Disable auxiliary device */
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

static bool ps2_mouse_enable(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_ENABLE);
}

static bool ps2_mouse_disable(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_DISABLE);
}

static bool ps2_mouse_reset(void)
{
    if (!ps2_mouse_send_command(PS2_MOUSE_RESET))
    {
        return false;
    }

    /* Wait for self-test result (should be 0xAA) */
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    if (ps2_controller_read_data() != PS2_MOUSE_SELF_TEST_OK)
    {
        return false;
    }

    /* Wait for mouse ID (should be 0x00) */
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    if (ps2_controller_read_data() != PS2_MOUSE_ID_STANDARD)
    {
        return false;
    }

    return true;
}

static bool ps2_mouse_set_defaults(void)
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

static bool ps2_mouse_set_resolution(uint8_t resolution)
{
    if (ps2_mouse_send_command_with_data(PS2_MOUSE_SET_RESOLUTION, resolution))
    {
        mouse_state.resolution = resolution;
        return true;
    }
    return false;
}

static bool ps2_mouse_set_scaling_2_1(void)
{
    return ps2_mouse_send_command(PS2_MOUSE_SET_SCALING_2_1);
}

void ps2_mouse_feed_byte(uint8_t data)
{
	struct ps2_mouse_pkt assembled;
	ps2_mouse_packet_t packet;
	static int packet_path_logged;

	if (!mouse_state.initialized)
	{
		ps2_mouse_pkt_reset(&pkt_state, expected_packet_size);
		return;
	}

	ps2_mouse_pkt_feed(&pkt_state, data, &assembled);
	if (!assembled.complete)
	{
#if DEBUG_PS2
		if (pkt_state.index == 0 && !(data & PS2_MOUSE_PKT_ALWAYS_1))
			kprintf("[MOUSE] resync drop=0x%02x\n", data);
#endif
		return;
	}

	packet.flags = assembled.flags;
	packet.delta_x = assembled.dx;
	packet.delta_y = assembled.dy;
	packet.delta_wheel = assembled.wheel;
	packet.extra_buttons = mouse_state.has_5buttons ? assembled.extra_buttons : 0;

#if DEBUG_PS2
	kprintf("[MOUSE] packet=[%02x %02x %02x] dx=%d dy=%d buttons=0x%02x\n",
		assembled.flags, (uint8_t)assembled.dx, (uint8_t)assembled.dy,
		(int)packet.delta_x, (int)packet.delta_y, packet.flags & 0x07);
#endif

	ps2_mouse_process_packet(&packet);
	if (!packet_path_logged)
	{
		packet_path_logged = 1;
		klog_info_fmt("INPUT", "PS2_MOUSE_PACKET_PATH_OK");
	}
}

static void ps2_mouse_process_packet(const ps2_mouse_packet_t *packet)
{
    if (!packet)
    {
        return;
    }

    /* Update button states */
    mouse_state.left_button = (packet->flags & PS2_MOUSE_LEFT_BUTTON) != 0;
    mouse_state.right_button = (packet->flags & PS2_MOUSE_RIGHT_BUTTON) != 0;
    mouse_state.middle_button = (packet->flags & PS2_MOUSE_MIDDLE_BUTTON) != 0;

    /* Update position (with bounds checking) */
    mouse_state.x += packet->delta_x;
    mouse_state.y -= packet->delta_y; /* Y is inverted in PS/2 */

    /* Clamp coordinates to reasonable bounds */
    if (mouse_state.x < 0)
    {
        mouse_state.x = 0;
    }
    if (mouse_state.y < 0)
    {
        mouse_state.y = 0;
    }
    if (mouse_state.x > 1023)
    {
        mouse_state.x = 1023;
    }
    if (mouse_state.y > 767)
    {
        mouse_state.y = 767;
    }

    /* Update wheel */
    if (mouse_state.has_wheel)
    {
        mouse_state.wheel = packet->delta_wheel;
    }

    /* Update extra buttons */
    if (mouse_state.has_5buttons)
    {
        mouse_state.button4 = (packet->extra_buttons & 0x01) != 0;
        mouse_state.button5 = (packet->extra_buttons & 0x02) != 0;
    }

    if (!mouse_dev)
    {
        return;
    }
    /* The core drops zero motion and unchanged buttons. */
    input_report_rel(mouse_dev, REL_X, packet->delta_x);
    input_report_rel(mouse_dev, REL_Y, -(int32_t)packet->delta_y);
    input_report_rel(mouse_dev, REL_WHEEL, packet->delta_wheel);
    input_report_key(mouse_dev, BTN_LEFT, mouse_state.left_button);
    input_report_key(mouse_dev, BTN_RIGHT, mouse_state.right_button);
    input_report_key(mouse_dev, BTN_MIDDLE, mouse_state.middle_button);
    input_report_key(mouse_dev, BTN_SIDE, mouse_state.button4);
    input_report_key(mouse_dev, BTN_EXTRA, mouse_state.button5);
    input_sync(mouse_dev);
}

static bool ps2_mouse_send_command(uint8_t command)
{
    /* Send command to mouse via controller */
    ps2_controller_write_command(PS2_CMD_WRITE_PORT2);
    if (!ps2_controller_wait_input())
    {
        return false;
    }

    ps2_controller_write_data(command);

    return ps2_mouse_wait_ack();
}

static bool ps2_mouse_send_command_with_data(uint8_t command, uint8_t data)
{
    if (!ps2_mouse_send_command(command))
    {
        return false;
    }

    /* Send data byte */
    ps2_controller_write_command(PS2_CMD_WRITE_PORT2);
    if (!ps2_controller_wait_input())
    {
        return false;
    }

    ps2_controller_write_data(data);

    return ps2_mouse_wait_ack();
}

static bool ps2_mouse_wait_ack(void)
{
    if (!ps2_controller_wait_output())
    {
        return false;
    }

    return ps2_controller_read_data() == PS2_MOUSE_ACK;
}

static bool ps2_controller_wait_input(void)
{
    int timeout = 1000;
    while (timeout-- > 0)
    {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0)
        {
            return true;
        }
        for (volatile int i = 0; i < 100; i++)
        {
            /* Spin */
        }
    }
    return false;
}

static bool ps2_controller_wait_output(void)
{
    int timeout = 1000;
    while (timeout-- > 0)
    {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)
        {
            return true;
        }
        for (volatile int i = 0; i < 100; i++)
        {
            /* Spin */
        }
    }
    return false;
}

static void ps2_controller_write_command(uint8_t command)
{
    ps2_controller_wait_input();
    outb(PS2_COMMAND_PORT, command);
}

static void ps2_controller_write_data(uint8_t data)
{
    ps2_controller_wait_input();
    outb(PS2_DATA_PORT, data);
}

static uint8_t ps2_controller_read_data(void)
{
    ps2_controller_wait_output();
    return inb(PS2_DATA_PORT);
}

static ps2_mouse_type_t ps2_mouse_detect_type(void)
{
    /* Try to enable wheel mouse mode */
    if (ps2_mouse_enable_wheel())
    {
        /* Check if 5-button mode is available */
        if (ps2_mouse_enable_5buttons())
        {
            return PS2_MOUSE_TYPE_5BUTTON;
        }
        return PS2_MOUSE_TYPE_WHEEL;
    }

    return PS2_MOUSE_TYPE_STANDARD;
}

static bool ps2_mouse_enable_wheel(void)
{
    /* Magic sequence to enable wheel: 200, 100, 80 */
    if (!ps2_mouse_set_sample_rate(PS2_MOUSE_WHEEL_SEQUENCE_1))
    {
        return false;
    }
    if (!ps2_mouse_set_sample_rate(PS2_MOUSE_WHEEL_SEQUENCE_2))
    {
        return false;
    }
    if (!ps2_mouse_set_sample_rate(PS2_MOUSE_WHEEL_SEQUENCE_3))
    {
        return false;
    }

    /* Get device ID */
    if (!ps2_mouse_send_command(PS2_MOUSE_GET_ID))
    {
        return false;
    }

    if (!ps2_controller_wait_output())
    {
        return false;
    }

    return ps2_controller_read_data() == PS2_MOUSE_ID_WHEEL;
}

static bool ps2_mouse_enable_5buttons(void)
{
    /* Magic sequence to enable 5-button mode: 200, 200, 80 */
    if (!ps2_mouse_set_sample_rate(PS2_MOUSE_5BTN_SEQUENCE_1))
    {
        return false;
    }
    if (!ps2_mouse_set_sample_rate(PS2_MOUSE_5BTN_SEQUENCE_2))
    {
        return false;
    }
    if (!ps2_mouse_set_sample_rate(PS2_MOUSE_5BTN_SEQUENCE_3))
    {
        return false;
    }

    /* Get device ID */
    if (!ps2_mouse_send_command(PS2_MOUSE_GET_ID))
    {
        return false;
    }

    if (!ps2_controller_wait_output())
    {
        return false;
    }

    return ps2_controller_read_data() == PS2_MOUSE_ID_5BUTTON;
}
