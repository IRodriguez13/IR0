/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/task.h>

struct process;

/* x86-only context diagnostics; never part of the portable switch contract. */
void switch_audit_user_frame_resume(struct process *prev,
                                    struct process *next,
                                    task_t *task);
void switch_trace_user_frame_resume(struct process *prev,
                                    struct process *next,
                                    task_t *task);
