/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: clock_wait.h
 * Description: Unified monotonic timer waits (nanosleep, poll timeout, clockevent wake)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct process;

#define IR0_CLOCK_WAIT_DISARMED UINT64_MAX

void ir0_clock_wait_arm(struct process *proc, uint64_t deadline_ms);
void ir0_clock_wait_disarm(struct process *proc);
void ir0_clock_wait_fire_due(uint64_t now_ms);

/*
 * Block current task until @deadline_ms (monotonic ms) or early wake.
 * Returns 0, -EINTR if syscall_interrupted, -ESRCH without current_process.
 */
int ir0_clock_wait_block_until(uint64_t deadline_ms);

/*
 * Advance clockevents only (safe from any syscall, including wait4 WNOHANG).
 */
void ir0_clock_wait_service_clockevents(void);

/*
 * Clockevents + runqueue rotation (blocking syscalls: poll, nanosleep, pause).
 */
void ir0_clock_wait_service_runqueue(void);

/* Legacy name used from clock_tick / idle loops. */
void sleep_wake_check(void);
