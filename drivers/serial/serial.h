/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Serial Port Driver
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for COM1 serial port (0x3F8)
 */

#pragma once

#include <stdint.h>

/* ========================================================================== */
/* PUBLIC API                                                                 */
/* ========================================================================== */

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *str);
void serial_print_hex32(uint32_t num);
void serial_print_hex64(uint64_t num);
char serial_read_char(void);
