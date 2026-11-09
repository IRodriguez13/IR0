/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context.h
 * Description: Context-switch facade (portable sched policy API).
 *
 * Callers model "change to the next runnable task", not "invoke the ISA switch
 * backend". The portable dispatcher selects an ISA backend through the
 * internal context backend contract; raw mechanics remain below arch/.
 *
 * Include this (or sched.h) from portable code.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>
#include <ir0/switch.h>

struct process;

void switch_to(task_t *prev, task_t *next);

/*
 * Prepare the portable per-task state before the initial ISA transfer from
 * boot/idle. The ISA owns address-space activation and the non-returning jump.
 */
void context_prepare_first(struct process *next);

/*
 * Enter userspace with full task register state (fork/signal/syscall-block resume).
 * The ISA backend performs the privilege return; portable code names only the
 * semantic transfer contract.
 */
void switch_to_user_task(const struct task *task);

void switch_to_user(uintptr_t entry, uintptr_t stack);

/*
 * First transfer from idle/boot into @next. Does not return on success.
 * ISA details live in arch backends; portable scheduling code must not embed a
 * privilege-return mechanism.
 */
void first_switch_to(struct process *next);
