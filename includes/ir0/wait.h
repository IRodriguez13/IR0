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

#define WNOHANG 1

#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) > 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status)    ((status) & 0x7f)

static inline int ir0_make_wait_status(int exit_code, int exit_signal)
{
	if (exit_signal > 0)
		return exit_signal & 0x7f;
	return (exit_code & 0xff) << 8;
}
