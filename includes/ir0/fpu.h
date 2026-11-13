/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fpu.h
 * Description: Per-task FP/SIMD register state (Linux x86 fpu, arm64
 *              fpsimd). Each ISA saves and restores the user FP/SIMD
 *              registers in __switch_to(), copies them on fork and keeps
 *              them across signal handlers; portable code only resets them
 *              when a new image starts.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

/*
 * fpu_flush_thread - Give @task the initial FP/SIMD state (Linux
 * fpu_flush_thread() at execve). When @task is running on this CPU its
 * live registers are reset too.
 */
void fpu_flush_thread(task_t *task);
