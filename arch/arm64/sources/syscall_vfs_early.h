/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_vfs_early.h
 * Description: Freestanding ARM64 VFS syscall provider for bring-up.
 */

#pragma once

#include <stdint.h>
#include <ir0/syscall_id.h>

int64_t arm64_early_vfs_syscall(void *context, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5);
