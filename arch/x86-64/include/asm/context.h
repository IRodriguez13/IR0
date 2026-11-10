/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

struct task;

/* asm/context/switch_x64.asm */
int switch_context_x64(struct task *prev, struct task *next);
void switch_to_user_task_asm(const struct task *task);

/* Called from switch_x64.asm when a resumed RIP is outside kernel text. */
void switch_report_bad_ret(uint64_t rip, struct task *task);
