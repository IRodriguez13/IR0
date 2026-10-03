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
#include <ir0/syscall_table.h>

extern const struct syscall_context_provider arm64_early_vfs_provider;
