/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: tty_raw_probe.c
 * Description: Raw TTY byte probe — Ctrl-X=0x18, arrows CSI (no nano hacks)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void tag(const char *s)
{
	int fd;
	const char *p = s;

	while (*p)
		p++;
	fd = open("/dev/serial", O_WRONLY);
	if (fd < 0)
		fd = 2;
	(void)write(fd, s, (size_t)(p - s));
	if (fd > 2)
		(void)close(fd);
}

static int set_raw(int fd, struct termios *saved)
{
	struct termios t;

	if (tcgetattr(fd, saved) != 0)
		return -1;
	t = *saved;
	cfmakeraw(&t);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	return tcsetattr(fd, TCSANOW, &t);
}

static int read_byte(int fd, unsigned char *out)
{
	ssize_t n = read(fd, out, 1);

	return (n == 1) ? 0 : -1;
}

int main(void)
{
	struct termios saved;
	unsigned char c;
	unsigned char esc[8];
	int i;
	int fd = STDIN_FILENO;

	tag("TTY_PROBE_START\n");

	if (set_raw(fd, &saved) != 0)
	{
		tag("TTY_PROBE_FAIL_TCSET\n");
		return 1;
	}

	tag("TTY_PROBE_WAIT_CTRLX\n");
	if (read_byte(fd, &c) != 0 || c != 0x18)
	{
		tag("TTY_PROBE_FAIL_CTRLX\n");
		(void)tcsetattr(fd, TCSANOW, &saved);
		return 2;
	}
	tag("TTY_PROBE_CTRLX_OK\n");

	tag("TTY_PROBE_WAIT_ARROW\n");
	/* Expect ESC [ A from Up arrow */
	for (i = 0; i < 3; i++)
	{
		if (read_byte(fd, &esc[i]) != 0)
		{
			tag("TTY_PROBE_FAIL_ARROW\n");
			(void)tcsetattr(fd, TCSANOW, &saved);
			return 3;
		}
	}
	if (esc[0] != 0x1b || esc[1] != '[' || esc[2] != 'A')
	{
		tag("TTY_PROBE_FAIL_ARROW\n");
		(void)tcsetattr(fd, TCSANOW, &saved);
		return 4;
	}
	tag("TTY_PROBE_ARROW_OK\n");

	tag("TTY_PROBE_WAIT_ENTER\n");
	/* Raw mode: Enter must be CR (0x0d), not LF — nano filename prompts. */
	if (read_byte(fd, &c) != 0 || c != 0x0d)
	{
		tag("TTY_PROBE_FAIL_ENTER\n");
		(void)tcsetattr(fd, TCSANOW, &saved);
		return 5;
	}
	tag("TTY_PROBE_ENTER_OK\n");

	if (tcsetattr(fd, TCSANOW, &saved) != 0)
	{
		tag("TTY_PROBE_FAIL_RESTORE\n");
		return 6;
	}
	tag("TTY_PROBE_OK\n");
	return 0;
}
