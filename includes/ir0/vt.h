/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vt.h
 * Description: Virtual terminal facade — text stream to the console screen.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void vt_init(void);
/* Clear the screen and the scrollback; reset cursor and SGR state. */
void vt_clear(uint8_t color);
/* Move the view @delta lines into the scrollback (positive = older). */
void vt_scroll(int delta);
/* Output stream (/dev/console, klog): colour comes from SGR sequences. */
void vt_putchar(char c, uint8_t color);
void vt_print(const char *str, uint8_t color);
/* Kernel text (print()): drawn with @color, recorded in the scrollback. */
void vt_putchar_attr(char c, uint8_t color);
void vt_show_cursor(uint8_t color);
int vt_cursor_x(void);
