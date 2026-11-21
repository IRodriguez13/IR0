/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: printk_console.h
 * Description: printk console registry — the sinks klog writes to.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Every rendered klog record, smoke tag and kprintf line. */
#define CON_RECORDS 0x1u
/* Only records worth showing on a screen (NOTICE and up, boot components). */
#define CON_MIRROR  0x2u
/* Raw panic-dump text (klog_print, klog_hex32, klog_hex64). */
#define CON_DUMP    0x4u

struct console
{
	const char *name;
	void (*write)(const char *str);
	unsigned int flags;
	struct console *next;
};

/*
 * Append @con to the registry; consoles receive text in registration order.
 * The serial console is registered statically with CON_RECORDS | CON_DUMP.
 * Boot-time only: the list is not locked.
 */
void register_console(struct console *con);

/* Write @str to every registered console whose flags include @flag. */
void console_write(unsigned int flag, const char *str);
