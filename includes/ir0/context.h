/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context.h
 * Description: Context-switch facade (no <sched/...> includes).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

struct process;

void arch_context_switch(task_t *prev, task_t *next);
uint64_t arch_get_current_page_directory(void);
void arch_set_current_kernel_stack(struct process *p);
