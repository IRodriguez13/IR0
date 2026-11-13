/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fpu_backend.h
 * Description: ISA hooks behind the per-task FP/SIMD state (arch/ only).
 *              Each ISA implements them in arch/<isa>/sources/fpu.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/fpu.h>

/* Boot: make FP/SIMD usable and load the initial state. */
void fpu_init(void);

/* __switch_to / first switch: save @prev's live registers, load @next's. */
void fpu_switch(task_t *prev, task_t *next);

/* fork: @child starts with the caller's live registers. */
void fpu_copy_current(task_t *child);
