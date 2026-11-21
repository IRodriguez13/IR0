/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <linux/input-event-codes.h>

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

/* PS/2 Status register bits (Linux i8042.h / OSDev dual-channel). */
#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02
#define PS2_STATUS_SYSTEM_FLAG  0x04
#define PS2_STATUS_COMMAND_DATA 0x08
#define PS2_STATUS_KEYLOCK      0x10
/* Bit 5: data in output buffer is from AUX (mouse), not keyboard. */
#define PS2_STATUS_AUXDATA      0x20
#define PS2_STATUS_TIMEOUT      0x40
#define PS2_STATUS_PARITY_ERROR 0x80

// PS/2 Configuration Byte bits
#define PS2_CFG_INT1            (1 << 0)
#define PS2_CFG_INT2            (1 << 1)
#define PS2_CFG_SYS             (1 << 2)
#define PS2_CFG_CLK1            (1 << 4)
#define PS2_CFG_CLK2            (1 << 5)
#define PS2_CFG_TRANS           (1 << 6)

// PS/2 Controller Responses
#define PS2_RESP_SELF_TEST_OK   0x55
#define PS2_RESP_PORT_TEST_OK   0x00
#define PS2_RESP_ACK            0xFA
#define PS2_RESP_RESET_DONE     0xAA

/*
 * Set-1 scan bytes whose names are absent from linux/input-event-codes.h.
 * Matching KEY_* values come from that header. Integers are unchanged.
 */
#define PS2_SCAN_EQUALS       0x0D
#define PS2_SCAN_LBRACKET     0x1A
#define PS2_SCAN_RBRACKET     0x1B
#define PS2_SCAN_LCTRL        0x1D
#define PS2_SCAN_QUOTE        0x28
#define PS2_SCAN_BACKTICK     0x29
#define PS2_SCAN_LSHIFT       0x2A
#define PS2_SCAN_PERIOD       0x34
#define PS2_SCAN_RSHIFT       0x36
#define PS2_SCAN_KP_MULTIPLY  0x37
#define PS2_SCAN_LALT         0x38
#define PS2_SCAN_KP_7         0x47
#define PS2_SCAN_KP_8         0x48
#define PS2_SCAN_KP_9         0x49
#define PS2_SCAN_KP_MINUS     0x4A
#define PS2_SCAN_KP_4         0x4B
#define PS2_SCAN_KP_5         0x4C
#define PS2_SCAN_KP_6         0x4D
#define PS2_SCAN_KP_PLUS      0x4E
#define PS2_SCAN_KP_1         0x4F
#define PS2_SCAN_KP_2         0x50
#define PS2_SCAN_KP_3         0x51
#define PS2_SCAN_KP_0         0x52
#define PS2_SCAN_KP_PERIOD    0x53
#define PS2_SCAN_EXTENDED     0xE0
#define PS2_SCAN_RELEASE      0x80

/* PS/2 driver ring size (distinct from config.h KEYBOARD_BUFFER_SIZE) */
#define PS2_KEYBOARD_BUFFER_SIZE 256

// Function prototypes
void ps2_init(void);
bool ps2_send_command(uint8_t command);
uint8_t ps2_read_data(void);

// Global variables
extern uint8_t keyboard_buffer[PS2_KEYBOARD_BUFFER_SIZE];
extern int keyboard_buffer_head;
extern int keyboard_buffer_tail;
extern bool keyboard_shift_pressed;
extern bool keyboard_ctrl_pressed;
extern bool keyboard_alt_pressed;
