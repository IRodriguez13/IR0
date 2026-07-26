/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_switch.h
 * Description: ISA backend for public switch_to() / kernel-stack handoff.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

struct process;

void arch_switch_to(task_t *prev, task_t *next);
void arch_set_current_kernel_stack(struct process *p);
void arch_switch_save_user_rsp(struct process *prev);
