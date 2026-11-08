/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

struct process;

/* Repair the x86 iretq image immediately before a direct user return. */
void x86_prepare_task_user_return(struct process *proc);

struct task;

/* asm/context/switch_x64.asm */
int switch_context_x64(struct task *prev, struct task *next);
void switch_to_user_task_asm(const struct task *task);
