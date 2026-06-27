/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_process.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "process.h"
#include "syscalls.h"
#include <ir0/scheduler_api.h>
#include <ir0/signals.h>
#include <ir0/wait.h>

static void ktest_wait4_child_entry(void)
{
	process_exit(42);
}

static void ktest_wait4_child_a_entry(void)
{
	process_exit(10);
}

static void ktest_wait4_child_b_entry(void)
{
	process_exit(20);
}

void ktest_boot_ok(void)
{
	KTEST_BEGIN("boot_ok");
	KASSERT(1);
	KTEST_END();
}

void ktest_process_current(void)
{
	KTEST_BEGIN("process_current");
	KASSERT(current_process != NULL);
	KASSERT_GE(current_process->task.pid, 0);
	int64_t pid = sys_getpid();
	KASSERT_EQ(pid, (int64_t)current_process->task.pid);
	KTEST_END();
}

void ktest_wait4_status(void)
{
	pid_t pid;
	pid_t waited;
	int status = -1;
	process_t *child;

	KTEST_BEGIN("wait4_status");
	pid = spawn_kernel(ktest_wait4_child_entry, "wait4_child");
	KASSERT_GT(pid, 0);

	/*
	 * Simulate child exit without driving the scheduler (avoids QEMU hang
	 * when init is the only runnable task). Exercises zombie reap + status.
	 */
	child = process_find_by_pid(pid);
	KASSERT(child != NULL);
	child->state = PROCESS_ZOMBIE;
	child->exit_code = 42;
	sched_remove_process(child);

	waited = process_wait(pid, &status, 0);
	KASSERT_EQ(waited, pid);
	KASSERT_EQ(status, (42 & 0xFF) << 8);
	KTEST_END();
}

void ktest_wait4_specific_reaps_requested_child(void)
{
	pid_t pid_a;
	pid_t pid_b;
	process_t *child_a;
	process_t *child_b;
	int status = -1;

	KTEST_BEGIN("wait4_specific_reaps_requested_child");
	pid_a = spawn_kernel(ktest_wait4_child_a_entry, "wait_a");
	pid_b = spawn_kernel(ktest_wait4_child_b_entry, "wait_b");
	KASSERT_GT(pid_a, 0);
	KASSERT_GT(pid_b, 0);

	child_b = process_find_by_pid(pid_b);
	KASSERT(child_b != NULL);
	child_b->state = PROCESS_ZOMBIE;
	child_b->exit_code = 20;
	sched_remove_process(child_b);

	current_process->wait_blocked = 1;
	current_process->wait_target_pid = pid_a;
	current_process->irq_frame_saved = 1;
	KASSERT_EQ(process_wait_child_matches_blocked_target(current_process, pid_b), 0);
	KASSERT_EQ(process_wait_child_matches_blocked_target(current_process, pid_a), 1);
	current_process->wait_blocked = 0;
	current_process->wait_target_pid = 0;
	current_process->irq_frame_saved = 0;

	child_a = process_find_by_pid(pid_a);
	KASSERT(child_a != NULL);
	child_a->state = PROCESS_ZOMBIE;
	child_a->exit_code = 10;
	sched_remove_process(child_a);

	KASSERT(process_find_by_pid(pid_b) != NULL);
	status = -1;
	KASSERT_EQ(process_wait(pid_a, &status, 0), pid_a);
	KASSERT_EQ(status, (10 & 0xFF) << 8);
	KASSERT(process_find_by_pid(pid_a) == NULL);
	KASSERT_EQ(process_wait(pid_b, &status, 0), pid_b);
	KASSERT(process_find_by_pid(pid_b) == NULL);
	KTEST_END();
}

void ktest_wait4_minus_one_reaps_any_child(void)
{
	pid_t pid_a;
	pid_t pid_b;
	process_t *child_b;
	int status = -1;

	KTEST_BEGIN("wait4_minus_one_reaps_any_child");
	pid_a = spawn_kernel(ktest_wait4_child_a_entry, "any_a");
	pid_b = spawn_kernel(ktest_wait4_child_b_entry, "any_b");
	KASSERT_GT(pid_a, 0);
	KASSERT_GT(pid_b, 0);

	child_b = process_find_by_pid(pid_b);
	KASSERT(child_b != NULL);
	child_b->state = PROCESS_ZOMBIE;
	child_b->exit_code = 20;
	sched_remove_process(child_b);

	status = -1;
	KASSERT_EQ(process_wait((pid_t)-1, &status, 0), pid_b);
	KASSERT_EQ(status, (20 & 0xFF) << 8);
	KASSERT(process_find_by_pid(pid_b) == NULL);
	KTEST_END();
}

void ktest_wait4_specific_no_leak_after_destroy(void)
{
	pid_t pid;
	process_t *child;

	KTEST_BEGIN("wait4_specific_no_leak_after_destroy");
	pid = spawn_kernel(ktest_wait4_child_entry, "destroy_child");
	KASSERT_GT(pid, 0);

	child = process_find_by_pid(pid);
	KASSERT(child != NULL);
	child->state = PROCESS_ZOMBIE;
	child->exit_code = 7;
	sched_remove_process(child);

	KASSERT_EQ(process_wait(pid, NULL, 0), pid);
	KASSERT(process_find_by_pid(pid) == NULL);
	KTEST_END();
}

void ktest_wait4_wnohang_specific(void)
{
	pid_t pid;
	process_t *child;

	KTEST_BEGIN("wait4_wnohang_specific");
	pid = spawn_kernel(ktest_wait4_child_a_entry, "wnohang_child");
	KASSERT_GT(pid, 0);

	child = process_find_by_pid(pid);
	KASSERT(child != NULL);
	KASSERT_EQ(child->state, PROCESS_READY);

	KASSERT_EQ(process_wait(pid, NULL, WNOHANG), 0);
	KASSERT(process_find_by_pid(pid) != NULL);
	KTEST_END();
}

void ktest_wait4_wnohang_echild_after_reap(void)
{
	pid_t pid;
	process_t *child;

	KTEST_BEGIN("wait4_wnohang_echild_after_reap");
	pid = spawn_kernel(ktest_wait4_child_entry, "echild_after_reap");
	KASSERT_GT(pid, 0);

	child = process_find_by_pid(pid);
	KASSERT(child != NULL);
	child->state = PROCESS_ZOMBIE;
	child->exit_code = 5;
	sched_remove_process(child);

	KASSERT_EQ(process_wait(pid, NULL, 0), pid);
	KASSERT(process_find_by_pid(pid) == NULL);
	KASSERT_EQ(process_wait(pid, NULL, WNOHANG), -ECHILD);
	KTEST_END();
}

void ktest_kill_sigterm_wait_status(void)
{
	pid_t pid;
	pid_t waited;
	int status = -1;
	process_t *child;

	KTEST_BEGIN("kill_sigterm_wait_status");
	pid = spawn_kernel(ktest_wait4_child_entry, "sigterm_zombie");
	KASSERT_GT(pid, 0);
	child = process_find_by_pid(pid);
	KASSERT(child != NULL);

	/* Simulate SIGTERM death encoding (same path as handle_signals + wait4). */
	child->state = PROCESS_ZOMBIE;
	child->exit_signal = SIGTERM;
	child->exit_code = 0;
	sched_remove_process(child);

	waited = process_wait(pid, &status, 0);
	KASSERT_EQ(waited, pid);
	KASSERT_EQ(status, 0x000f);
	KASSERT(WIFSIGNALED(status));
	KASSERT_EQ(WTERMSIG(status), SIGTERM);
	KASSERT(process_find_by_pid(pid) == NULL);

	/* send_signal must wake a blocked target with pending SIGTERM. */
	pid = spawn_kernel(ktest_wait4_child_entry, "sigterm_wake");
	KASSERT_GT(pid, 0);
	child = process_find_by_pid(pid);
	KASSERT(child != NULL);
	child->state = PROCESS_BLOCKED;
	KASSERT_EQ(send_signal(pid, SIGTERM), 0);
	KASSERT_EQ(child->state, PROCESS_READY);
	KASSERT(child->signal_pending & SIGNAL_MASK(SIGTERM));
	child->state = PROCESS_ZOMBIE;
	child->exit_code = 0;
	sched_remove_process(child);
	KASSERT_EQ(process_wait(pid, NULL, 0), pid);
	KTEST_END();
}
