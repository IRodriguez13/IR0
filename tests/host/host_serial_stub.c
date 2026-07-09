/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: host_serial_stub.c
 * Description: No-op serial_io stubs for host-linked kernel sources
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/serial_io.h>

void serial_init(void)
{
}

void serial_putchar(char c)
{
	(void)c;
}

void serial_print(const char *str)
{
	(void)str;
}

void serial_print_hex32(uint32_t num)
{
	(void)num;
}

void serial_print_hex64(uint64_t num)
{
	(void)num;
}

char serial_read_char(void)
{
	return 0;
}

int serial_try_read_char(void)
{
	return -1;
}
