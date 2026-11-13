/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: timer.h
 * Description: ARM64 generic-timer hooks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/cpu_info.h>
#include <ir0/cpu.h>

int arch_timer_smoke_ok(void);

/** Arm EL1 physical timer one-shot (CNTP_TVAL + ENABLE, IMASK clear). */
void arch_timer_oneshot_arm(uint32_t ticks);

/** Disable physical timer (clear ENABLE). */
void arch_timer_oneshot_disarm(void);

/** Periodic EL1 physical timer at CONFIG_TICK_RATE_HZ (CNTP_TVAL reload). */
void arch_timer_periodic_arm(void);
