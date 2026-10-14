/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

struct process;

/* Repair the x86 iretq image immediately before a direct user return. */
void x86_prepare_task_user_return(struct process *proc);
