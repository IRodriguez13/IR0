/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmap.h
 * Description: Per-process VMA list operations owned by mm/.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

int mm_detach_vma_range(uintptr_t start, uintptr_t end);
int mm_apply_vma_prot(uintptr_t start, uintptr_t end, int prot);
int mm_prepare_map_fixed(uintptr_t start, size_t length);
