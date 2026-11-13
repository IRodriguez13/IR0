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

struct sigcontext;

/* Boot: make FP/SIMD usable and load the initial state. */
void fpu_init(void);

/* __switch_to / first switch: save @prev's live registers, load @next's. */
void fpu_switch(task_t *prev, task_t *next);

/* fork: @child starts with the caller's live registers. */
void fpu_copy_current(task_t *child);

/* Signal delivery: the interrupted registers go into @ctx. */
void fpu_save_sigcontext(struct sigcontext *ctx);

/*
 * Handler entry: give the handler its starting FP state. x86-64 loads the
 * initial state (Linux fpu__clear_user_states()); arm64 keeps the
 * interrupted registers, as Linux does.
 */
void fpu_signal_enter(void);

/*
 * rt_sigreturn: make the FP state in the user-supplied @ctx safe to load.
 * Returns 0 when it is malformed (the frame is then refused).
 */
int fpu_sanitize_sigcontext(struct sigcontext *ctx);

/* rt_sigreturn: @task resumes with the FP registers saved in @ctx. */
void fpu_load_sigcontext(task_t *task, const struct sigcontext *ctx);
