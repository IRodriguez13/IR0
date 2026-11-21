/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: tty_mirror.c
 * Description: TTY serial mirror — copies console glyphs to the serial port.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/console.h>
#include <ir0/serial_io.h>
#include <config.h>
#include <stddef.h>

#ifndef CONFIG_CONSOLE_SERIAL_MIRROR
#define CONFIG_CONSOLE_SERIAL_MIRROR 1
#endif

/* After handoff, do not mirror TTY glyphs to serial (human console vs bootlog). */
static int tty_serial_mirror = 1;

void tty_serial_mirror_set(int on)
{
	tty_serial_mirror = on ? 1 : 0;
}

void tty_serial_mirror_handoff(void)
{
#if !CONFIG_CONSOLE_SERIAL_MIRROR
	tty_serial_mirror = 0;
#endif
	/*
	 * Keep TTY→COM1 mirror when CONFIG_CONSOLE_SERIAL_MIRROR=y so headless
	 * smokes can grep prompts; serial_putchar itself must not spin forever
	 * if the host stdio pipe backs up.
	 */
}

void tty_serial_mirror_bytes(const char *str, size_t len)
{
	size_t i;

	if (!str || !tty_serial_mirror)
		return;
	for (i = 0; i < len; i++)
	{
		if (str[i] == '\n')
			serial_putchar('\r');
		serial_putchar(str[i]);
	}
}
