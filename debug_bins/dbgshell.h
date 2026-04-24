/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dbgshell.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Built-in Shell
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for kernel shell
 */

#pragma once
#include <stdint.h>


void shell_entry(void);
void cmd_clear(void);
void vga_print(const char *str, uint8_t color);
