/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_KEYBOARD_H
#define _IR0_KEYBOARD_H

/* Keyboard buffer functions */
extern char keyboard_buffer_get(void);
extern int keyboard_buffer_has_data(void);
extern int keyboard_set_layout(int layout);
extern int keyboard_get_layout(void);
extern const char *keyboard_get_layout_name(int layout);

#endif /* _IR0_KEYBOARD_H */