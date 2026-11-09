/**
 * IR0 Kernel — Core system software
 *
 * File: context_backend.h
 * Description: Narrow ISA backend hooks for the portable context state machine.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

struct process;

/*
 * These hooks deliberately express scheduling events, not registers or
 * instructions. sched/switch owns the state machine; each ISA owns the raw
 * checkpoint, stack handoff, and frame repair.
 */
void context_backend_switch(task_t *prev, task_t *next);
uint64_t context_backend_user_return_value(const struct process *proc,
                                           const task_t *task);
void context_backend_prepare_kernel_resume(task_t *task);
void context_backend_inject_kernel_return_fault(task_t *task);
