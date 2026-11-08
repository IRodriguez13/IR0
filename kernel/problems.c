/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: problems.c
 * Description: BUG/BUG_ON/OOPS reporters. They only classify the report and
 *              hand it to panicex(); halting, the banner and nested faults
 *              stay in kernel/oops.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/problems.h>
#include <ir0/oops.h>

void bug_report(const char *what, const char *file, int line,
		const char *func)
{
	panicex(what ? what : "BUG", PANIC_KERNEL_BUG, file, line, func);
	__builtin_unreachable();
}

void oops_report(const char *msg, const char *file, int line,
		 const char *func)
{
	panicex(msg ? msg : "OOPS", PANIC_KERNEL_BUG, file, line, func);
	__builtin_unreachable();
}
