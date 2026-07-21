/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: slice_hello.c
 * Description: Minimal post-MMU TU for F7b ARM64_SLICE_OBJS + serial_io proof.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "slice_hello.h"

#include <ir0/serial_io.h>
#include <ir0/ktm/klog.h>

void arm64_slice_after_mmu(void)
{
	klog_smoke("ARM64_SLICE_OK");
}
