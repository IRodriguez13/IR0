/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_backend.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Input backend API
 *
 * Driver-agnostic mouse entrypoints used by core/kernel layers.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct ir0_mouse_state
{
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
    uint8_t type;
    bool initialized;
} ir0_mouse_state_t;

void input_mouse_handle_interrupt(void);
bool input_mouse_is_available(void);
bool input_mouse_get_state(ir0_mouse_state_t *out);
bool input_mouse_set_sensitivity(uint8_t sensitivity);
