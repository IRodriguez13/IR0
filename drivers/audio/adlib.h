/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: adlib.h
 * Description: Adlib (Yamaha YM3812 OPL2) audio driver header
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Adlib OPL2 I/O Ports */
#define ADLIB_ADDRESS_PORT    0x388
#define ADLIB_DATA_PORT       0x389

/* OPL2 Registers */
#define ADLIB_REG_TEST        0x01
#define ADLIB_REG_TIMER1      0x02
#define ADLIB_REG_TIMER2      0x03
#define ADLIB_REG_TIMER_CTRL  0x04
#define ADLIB_REG_FM_MODE     0x08

/* Timer Control bits */
#define ADLIB_TIMER1_MASK     0x40
#define ADLIB_TIMER2_MASK     0x20
#define ADLIB_TIMER1_RST      0x80
#define ADLIB_TIMER2_RST      0x40
#define ADLIB_IRQ_RESET       0x80

/* Function prototypes */
bool adlib_init(void);
void adlib_shutdown(void);
bool adlib_is_available(void);
void adlib_write(uint8_t reg, uint8_t value);
uint8_t adlib_read(uint8_t reg);
bool adlib_detect(void);

