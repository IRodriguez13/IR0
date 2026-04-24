/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console_backend.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Console backend facade
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

void console_backend_init(void);
void console_backend_clear(uint8_t color);
int console_backend_uses_framebuffer(void);
void console_backend_scroll(int lines);
void console_backend_write(const char *str, size_t len, uint8_t color);

