/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_mm_early.h
 * Description: Freestanding ARM64 MM syscall provider for bring-up.
 */

#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

extern const struct syscall_context_provider arm64_early_mm_provider;
void arm64_early_mm_reset_busybox_heap(void);
