/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: d1_13_malloc_pf_diag.h
 * Description: D1.13 memmove PF / mallocng forensics (temporary)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct process;

void d1_13_malloc_pf_diag(struct process *p, uint64_t fault_addr, uint64_t rip,
			  uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx);
