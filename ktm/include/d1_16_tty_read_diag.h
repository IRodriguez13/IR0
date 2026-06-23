/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: d1_16_tty_read_diag.h
 * Description: D1.16 TTY read/wake Linux-like diagnostics
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct process;

void d1_16_tty_read_pre(struct process *p, int fd, int nonblock,
			uintptr_t tty_ptr);
void d1_16_tty_read_block(struct process *p, int waiters, const char *reason);
void d1_16_tty_line_ready(uintptr_t tty_ptr, size_t queued);
void d1_16_tty_wake(int waiters_before, int woke, uint32_t pid);
void d1_16_tty_read_resume(struct process *p, const char *reason);
void d1_16_tty_state_transition(struct process *p, int from_state,
				int to_state);
