/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_backend.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * File: input_backend.c
 * IR0 Kernel - Input backend API implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * Description: This file implements the input backend API for the IR0 kernel.
 *              It provides a common interface for input devices to be used by
 *              the kernel.
 *              It is used to handle the input devices and to provide a common
 *              interface for the input devices.
 *              It is used to handle the input devices and to provide a common
 */

#include <ir0/input_backend.h>
#include <config.h>
#if CONFIG_ENABLE_MOUSE
#include <drivers/IO/ps2_mouse.h>
#endif

void input_mouse_handle_interrupt(void)
{
#if CONFIG_ENABLE_MOUSE
    ps2_mouse_handle_interrupt();
#endif
}

bool input_mouse_is_available(void)
{
#if CONFIG_ENABLE_MOUSE
    return ps2_mouse_is_available();
#else
    return false;
#endif
}

bool input_mouse_get_state(ir0_mouse_state_t *out)
{
    if (!out)
        return false;
#if CONFIG_ENABLE_MOUSE
    if (!ps2_mouse_is_available())
        return false;
    ps2_mouse_state_t *st = ps2_mouse_get_state();
    if (!st)
        return false;
    out->x = st->x;
    out->y = st->y;
    out->wheel = st->wheel;
    out->left_button = st->left_button;
    out->right_button = st->right_button;
    out->middle_button = st->middle_button;
    out->button4 = st->button4;
    out->button5 = st->button5;
    out->has_wheel = st->has_wheel;
    out->has_5buttons = st->has_5buttons;
    out->resolution = st->resolution;
    out->sample_rate = st->sample_rate;
    out->type = (uint8_t)st->type;
    out->initialized = st->initialized;
    return true;
#else
    return false;
#endif
}

bool input_mouse_set_sensitivity(uint8_t sensitivity)
{
#if CONFIG_ENABLE_MOUSE
    return ps2_mouse_set_sample_rate(sensitivity);
#else
    (void)sensitivity;
    return false;
#endif
}
