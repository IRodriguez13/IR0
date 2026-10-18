/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_early.h
 * Description: Freestanding EL0 SVC dispatch (Linux aarch64 nr in x8).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

struct early_user_services;

/* Replace the freestanding usercopy provider after product MM is available. */
void arm64_syscall_usercopy_bind(const struct early_user_services *user);
void arm64_syscall_bind_identity_provider(const struct syscall_context_provider *p);
void arm64_syscall_bind_cwd_provider(const struct syscall_context_provider *p);
void arm64_syscall_bind_vfs_path_provider(const struct syscall_context_provider *p);

/**
 * Dispatch one SVC. Returns value for x0.
 * Sets *leave_el0 nonzero to retarget eret to EL1 continuation.
 */
int64_t arm64_syscall_early(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
			    uint64_t a3, uint64_t a4, uint64_t a5, int *leave_el0);

/** Nonzero after successful getpid+write (for ARM64_SYSCALL_OK). */
int arm64_syscall_smoke_ok(void);
