/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: d1_12_read_diag.h
 * Description: D1.12 read/syscall-return integrity diagnostics (temporary)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct process;

void d1_12_read_diag_syscall_pre(struct process *p, int fd, uintptr_t buf,
				 size_t count, uint64_t rip);
void d1_12_read_diag_syscall_post(struct process *p, int64_t ret);
void d1_12_read_diag_tty_line(size_t line_len, const char *kline,
			      size_t kline_len);
void d1_12_read_diag_kcopy(int64_t ret, size_t req_count,
			   const char *kbuf, size_t kcopy_len);
void d1_12_read_diag_pf(struct process *p, uint64_t fault_rip, uint64_t rdx,
			uint64_t rsi, uint64_t rdi);
