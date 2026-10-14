/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch.h
 * Description: Context-switch backend hooks (public simple names).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

struct process;

void set_current_kernel_stack(struct process *p);
void switch_save_user_rsp(struct process *prev);

/*
 * switch_report_bad_ret — invalid kernel continuation outside executable text.
 * Called from ISA switch assembly only; portable sched uses switch_to().
 */
void switch_report_bad_ret(uint64_t rip, task_t *task);
