/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context.h
 * Description: Context-switch facade (Linux switch_to model).
 *
 * switch_to(prev, next) saves @prev's kernel continuation and resumes
 * @next's on its own kernel stack; it returns when @prev runs again. With
 * prev == NULL it is the first transfer from boot and does not return.
 * A task returns to userspace through its own syscall/exception exit, or
 * on its first run through switch_to_user_task() (Linux ret_from_fork).
 * ISA hooks live in <ir0/context_backend.h>, never in portable callers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

void switch_to(task_t *prev, task_t *next);

/* Enter userspace with @task's saved user register state (fork, signals). */
void switch_to_user_task(const struct task *task);

/* Enter userspace at @entry with stack @stack (first image of a task). */
void switch_to_user(uintptr_t entry, uintptr_t stack);
