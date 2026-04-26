/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: context.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_CONTEXT_H
#define _IR0_CONTEXT_H

#include <kernel/scheduler/task.h>

/* Context switching functions */
extern void switch_context_x64(task_t *prev, task_t *next);
extern void switch_context_arm64(task_t *prev, task_t *next);
void arch_context_switch(task_t *prev, task_t *next);
extern uint64_t get_current_page_directory(void);
uint64_t arch_get_current_page_directory(void);

#endif /* _IR0_CONTEXT_H */