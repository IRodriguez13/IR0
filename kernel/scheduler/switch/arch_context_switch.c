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
#include <kernel/process.h>
#include <arch/common/arch_portable.h>

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
    process_t *prev_proc;
    process_t *next_proc;

    if (next)
    {
        next_proc = task_to_process(next);
        if (next->cr3 == 0 && next_proc->page_directory)
            next->cr3 = (uint64_t)(uintptr_t)next_proc->page_directory;
    }

    prev_proc = prev ? task_to_process(prev) : NULL;

    /*
     * Syscall-block resume (wait4, fork child): Linux ret_from_fork path.
     * CR3 is loaded in arch_switch_to_user_task_asm / switch_context_x64.
     */
    if (prev_proc && prev_proc->irq_frame_saved)
    {
        prev_proc->irq_frame_saved = 0;
        if (next)
            arch_switch_to_user_task(next);
        return;
    }

    switch_context_x64(prev, next);
#endif
}

uint64_t arch_get_current_page_directory(void)
{
    return get_current_page_directory();
}
