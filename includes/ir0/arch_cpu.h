/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_cpu.h
 * Description: Compatibility umbrella for ISA facades. New code includes the
 *              domain header (cpu.h, irq.h, arch_mm.h, context.h, …).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/arch_types.h>
#include <ir0/arch_config.h>
#include <ir0/early_clock.h>
#include <ir0/multiboot.h>
#include <ir0/tlb.h>
#include <ir0/tls.h>
#include <ir0/cpu_info.h>
#include <ir0/irq.h>
#include <ir0/arch_io.h>
#include <ir0/arch_mm.h>
#include <ir0/page_fault.h>
#include <ir0/arch_info.h>

/*
 * Context-switch entry lives in context.h. Forward decls here keep this
 * umbrella from pulling task.h into every legacy include.
 */
struct task;
struct process;

void switch_to_user(arch_addr_t entry, arch_addr_t stack);
void switch_to_user_task(const struct task *task);
void prepare_task_user_iretq(struct process *proc);
void first_switch_to(struct process *next);
