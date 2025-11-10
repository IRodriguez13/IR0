/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Built-in Shell
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for kernel shell
 */

#pragma once
#include <stdint.h>

/* ========================================================================== */
/* PUBLIC API                                                                 */
/* ========================================================================== */

void shell_entry(void);
void cmd_clear(void);
void vga_print(const char *str, uint8_t color);
