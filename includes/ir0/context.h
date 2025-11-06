/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_CONTEXT_H
#define _IR0_CONTEXT_H

#include <kernel/scheduler/task.h>

/* Context switching functions */
extern void switch_context_x64(task_t *prev, task_t *next);
extern uint64_t get_current_page_directory(void);

#endif /* _IR0_CONTEXT_H */