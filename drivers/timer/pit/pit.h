/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pit.h
 * Description: IR0 kernel source/header file
 */

#pragma once
#include <stdint.h>

/* PIT (Programmable Interval Timer) Registers */
#define PIT_REG_CHAN0   0x40
#define PIT_REG_COMMAND 0x43

/* PIT Commands */
#define PIT_CMD_CHAN0   0x00
#define PIT_CMD_LOHI    0x30
#define PIT_CMD_MODE3   0x06
#define PIT_COMMAND_VAL (PIT_CMD_CHAN0 | PIT_CMD_LOHI | PIT_CMD_MODE3)

#define PIT_BASE_FREC   1193180

void init_PIT(uint32_t frequency);
void increment_pit_ticks(void);

