/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sigtramp.h
 * Description: Kernel-provided rt_sigreturn trampoline for handlers
 *              installed without SA_RESTORER (Linux arm64 vDSO sigtramp).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#include <ir0/abi/signal_contract.h>

struct process;

/*
 * User address handlers without SA_RESTORER return through, or 0 when the
 * ISA ABI requires SA_RESTORER (Linux x86-64).
 */
static inline uint64_t sigtramp_addr(void)
{
	return IR0_ARCH_SIGTRAMP_HAVE ? (uint64_t)IR0_ARCH_SIGTRAMP_USER_BASE : 0;
}

/*
 * Map the trampoline page read-only/executable into @proc at exec.
 * Returns 0, -ENOSYS when the ISA has no trampoline, or -errno.
 */
int sigtramp_map(struct process *proc);
