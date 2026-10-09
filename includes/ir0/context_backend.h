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
 * instructions.  sched/switch owns the state machine; each ISA owns the raw
 * checkpoint and frame repair.
 */
int context_backend_checkpoint(task_t *prev);
void context_backend_prepare_user_frame(struct process *proc, task_t *task);
