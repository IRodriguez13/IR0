/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tty_smoke.c
 * Description: Capped serial smoke tags for the console TTY path, armed by
 *              ir0.tty_smoke=1 once userspace owns the console. They report
 *              what the line discipline and read(2) did, never which
 *              program asked: the harness reads the mirrored console output
 *              for that (scripts/serial_shell_tags.py).
 */

#include <ir0/tty_smoke.h>
#include <ir0/cmdline.h>
#include <ir0/console.h>
#include <ir0/ktm/klog.h>

#define TTY_SMOKE_LINE_CAP 4
#define TTY_SMOKE_READ_CAP 8

int ir0_tty_smoke_active(void)
{
	return ir0_cmdline_tty_smoke_enabled() && ir0_console_in_userspace();
}

void ir0_tty_smoke_line_ready(void)
{
	static int count;

	if (!ir0_tty_smoke_active() || count >= TTY_SMOKE_LINE_CAP)
		return;
	count++;
	klog_smoke("TTY_CANON_LINE_READY");
}

void ir0_tty_smoke_read_return(int fd, int64_t ret)
{
	static int count;

	if (!ir0_tty_smoke_active() || fd != 0 || ret <= 0 ||
	    count >= TTY_SMOKE_READ_CAP)
		return;
	count++;
	klog_smoke("SYS_READ_RETURN_OK");
}
