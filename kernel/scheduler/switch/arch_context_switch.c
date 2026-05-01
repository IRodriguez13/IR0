/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_context_switch.c
 * Description: Architecture-dispatched context switch wrappers (ISA stubs are private).
 */

#include <kernel/scheduler/task.h>

#if defined(ARCH_ARM64)
extern void switch_context_arm64(task_t *prev, task_t *next);
#else
extern void switch_context_x64(task_t *prev, task_t *next);
#endif

extern uint64_t get_current_page_directory(void);

void arch_context_switch(task_t *prev, task_t *next)
{
#if defined(ARCH_ARM64)
    switch_context_arm64(prev, next);
#else
    switch_context_x64(prev, next);
#endif
}

uint64_t arch_get_current_page_directory(void)
{
    return get_current_page_directory();
}
