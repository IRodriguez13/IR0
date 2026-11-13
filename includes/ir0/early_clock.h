/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_clock.h
 * Description: Pre-calibration monotonic counter facade (klog / boot timing).
 *
 * Portable callers use semantic names; x86 reads TSC, ARM64 reads CNTVCT,
 * without embedding ISA in the API.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Earliest ordered counter before PIT/HPET calibration.
 * Raw ticks, not wall-clock time, until clock_is_ready().
 */
int early_clock_available(void);
uint64_t early_clock_read(void);
