/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console.c
 * Description: printk console registry and the serial console.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/printk_console.h>
#include <ir0/serial_io.h>
#include <stddef.h>

static void serial_console_write(const char *str)
{
	serial_print(str);
}

static struct console serial_console = {
	.name = "serial",
	.write = serial_console_write,
	.flags = CON_RECORDS | CON_DUMP,
};

static struct console *console_list = &serial_console;

void register_console(struct console *con)
{
	struct console **link = &console_list;

	if (!con || !con->write)
		return;
	while (*link)
	{
		if (*link == con)
			return;
		link = &(*link)->next;
	}
	con->next = NULL;
	*link = con;
}

void console_write(unsigned int flag, const char *str)
{
	struct console *con;

	if (!str)
		return;
	for (con = console_list; con; con = con->next)
	{
		if (con->flags & flag)
			con->write(str);
	}
}
