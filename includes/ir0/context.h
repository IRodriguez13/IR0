/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context.h
 * Description: Context-switch facade (portable sched policy API).
 *
 * Callers model "change to the next runnable task", not "invoke the ISA switch
 * backend". Implementation is selected at link time (switch_x64.asm /
 * switch_context_arm64.S) via switch_to() → arch_switch_to() in the
 * sched/switch dispatcher only.
 *
 * Include this (or sched.h), not arch_switch.h, from portable code.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>
#include <ir0/switch.h>

struct process;

/*
 * Semantic route selected from generic process state.  It does not describe
 * an ISA return instruction or a register layout; those remain backend work.
 */
enum context_resume_route {
	CONTEXT_RESUME_SWITCH = 0,
	CONTEXT_RESUME_KERNEL,
	CONTEXT_RESUME_USER_FRAME,
};

int process_context_waits_for_child(const struct process *proc);
enum context_resume_route process_context_resume_route(const struct process *proc);

/* Prepare/finish portable process state around a backend user-frame return. */
void context_prepare_user_frame_resume(task_t *next);
void context_finish_user_frame_resume(struct process *proc);

void switch_to(task_t *prev, task_t *next);

/*
 * Prepare the portable per-task state before the initial ISA transfer from
 * boot/idle. The ISA owns address-space activation and the non-returning jump.
 */
void context_prepare_first(struct process *next);

/*
 * Enter userspace with full task register state (fork/signal/syscall-block resume).
 * ISA backend performs iretq / EL drop; portable code names the contract only.
 */
void switch_to_user_task(const struct task *task);

void switch_to_user(uintptr_t entry, uintptr_t stack);

/* Reapply syscall_frame GPRs before ring-3 iretq when task.arch has kstack residue. */
void prepare_task_user_iretq(struct process *proc);

/*
 * First transfer from idle/boot into @next. Does not return on success.
 * ISA details live in arch backends; portable sched must not embed iretq.
 */
void first_switch_to(struct process *next);
