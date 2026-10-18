/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: product_vfs.h
 * Description: Product CHDIR / OPENAT / STAT provider (path-checked).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/syscall_table.h>

extern const struct syscall_context_provider ir0_product_vfs_provider;
extern const struct syscall_context_provider early_vfs_path_provider;
