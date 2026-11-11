/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: wait.h
 * Description: POSIX wait status encoding (Linux-compatible)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#ifndef WNOHANG
#define WNOHANG 1
#endif

/* wait4(2) option bits Linux accepts (kernel/exit.c kernel_wait4()). */
#define WAIT4_VALID_OPTIONS \
	(WNOHANG | 0x00000002 /* WUNTRACED */ | 0x00000008 /* WCONTINUED */ | \
	 0x20000000 /* __WNOTHREAD */ | 0x40000000 /* __WALL */ | \
	 0x80000000U /* __WCLONE */)

#ifndef WIFEXITED
#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(status) (((status) & 0x7f) > 0 && ((status) & 0x7f) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(status)    ((status) & 0x7f)
#endif

static inline int ir0_make_wait_status(int exit_code, int exit_signal)
{
	if (exit_signal > 0)
		return exit_signal & 0x7f;
	return (exit_code & 0xff) << 8;
}
