/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: serial_io_arm64.c
 * Description: Freestanding serial_io facade backed by PL011 (not COM1).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "pl011.h"

#include <stdint.h>

/* Match includes/ir0/serial_io.h symbols for freestanding link. */

void serial_init(void)
{
	pl011_init();
}

void serial_putchar(char c)
{
	if (c == '\n')
	{
		pl011_putc('\r');
	}
	pl011_putc(c);
}

void serial_print(const char *str)
{
	pl011_puts(str);
}

void serial_print_hex32(uint32_t num)
{
	static const char hex[] = "0123456789ABCDEF";
	char buf[9];
	int i;

	for (i = 7; i >= 0; i--)
	{
		buf[i] = hex[num & 0xFu];
		num >>= 4;
	}
	buf[8] = '\0';
	pl011_puts(buf);
}

void serial_print_hex64(uint64_t num)
{
	serial_print_hex32((uint32_t)(num >> 32));
	serial_print_hex32((uint32_t)num);
}

char serial_read_char(void)
{
	return 0;
}

int serial_try_read_char(void)
{
	return -1;
}
