/**
 * IR0 Kernel — Core system software
 *
 * File: context_backend.h
 * Description: ISA hooks behind switch_to() (sched/switch and arch/ only).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

struct process;

/*
 * Linux __switch_to/cpu_switch_to: save @prev's kernel continuation, load
 * @next's (address space, stack, registers) and continue it.
 */
void __switch_to(task_t *prev, task_t *next);

/* First transfer from boot into @next; does not return. */
void first_switch_to(struct process *next);

/* Point the CPU's kernel-entry stack at @p's kernel stack. */
void set_current_kernel_stack(struct process *p);

/*
 * Class B recovery hooks for kernel_resume.c: repair the ISA return image,
 * inject the KTM test fault, and report the syscall result to rebuild.
 */
void context_backend_prepare_kernel_resume(task_t *task);
void context_backend_inject_kernel_return_fault(task_t *task);
uint64_t context_backend_user_return_value(const struct process *proc,
                                           const task_t *task);
