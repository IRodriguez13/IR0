/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: problems.h
 * Description: Kernel bug reporting (BUG, BUG_ON, OOPS). Portable; every
 *              report ends in panic(), which owns halting and nested faults.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Report a broken kernel invariant and panic. Never returns. */
void bug_report(const char *what, const char *file, int line,
		const char *func) __attribute__((noreturn));

/* Report an unrecoverable kernel error with a message and panic. */
void oops_report(const char *msg, const char *file, int line,
		 const char *func) __attribute__((noreturn));

#define BUG() bug_report("BUG", __FILE__, __LINE__, __func__)

#define BUG_ON(condition)                                                    \
	do                                                                   \
	{                                                                    \
		if (__builtin_expect(!!(condition), 0))                       \
			bug_report("BUG_ON: " #condition, __FILE__, __LINE__,  \
				   __func__);                                \
	} while (0)

#define OOPS(msg) oops_report((msg), __FILE__, __LINE__, __func__)
