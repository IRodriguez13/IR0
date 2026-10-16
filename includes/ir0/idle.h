/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: idle.h
 * Description: Portable idle-task owners (Linux idle loop / do_exit fallback).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Linux kernel/exit.c do_exit() ends in do_task_dead() → schedule().
 * The CPU then runs the idle task (docs.kernel.org/admin-guide/pm/cpuidle.html).
 * Product images bind poll (console/pipe/net). A harness may bind
 * on_quiescent instead of spinning when PID 1 is already a zombie.
 */
struct process_idle_services
{
	void (*poll)(void);
	void (*on_quiescent)(void);
};

void process_idle_bind(const struct process_idle_services *ops);
const struct process_idle_services *process_idle_services(void);
void kernel_idle_loop(void);
