/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Process resource lifetime facade.  Process policy publishes lifecycle
 * transitions; this file composes common cleanup and optional owners.
 */

#include "process_internal.h"
#include <ir0/process_exit.h>

static struct process_exit_services g_exit_ops;

void process_exit_bind_services(const struct process_exit_services *ops)
{
	if (!ops)
	{
		g_exit_ops.exit_robust_list = 0;
		g_exit_ops.clear_child_tid = 0;
		g_exit_ops.purge_console_waiters = 0;
		g_exit_ops.purge_pipe_waiters = 0;
		g_exit_ops.purge_ipc_waiters = 0;
		g_exit_ops.clear_console_session = 0;
		g_exit_ops.disarm_clock_wait = 0;
		g_exit_ops.tcp_on_exit = 0;
		g_exit_ops.notify_signal = 0;
		return;
	}
	g_exit_ops = *ops;
}

const struct process_exit_services *process_exit_services(void)
{
	return &g_exit_ops;
}

void process_resources_release_on_exit(process_t *dying)
{
	if (!dying)
		return;

	if (g_exit_ops.tcp_on_exit)
		g_exit_ops.tcp_on_exit(dying);
	if (g_exit_ops.exit_robust_list)
		g_exit_ops.exit_robust_list(dying);
	if (g_exit_ops.clear_child_tid)
		g_exit_ops.clear_child_tid(dying);
	if (g_exit_ops.purge_console_waiters)
		g_exit_ops.purge_console_waiters(dying);
	if (g_exit_ops.purge_pipe_waiters)
		g_exit_ops.purge_pipe_waiters(dying);
	if (g_exit_ops.purge_ipc_waiters)
		g_exit_ops.purge_ipc_waiters(dying);
	if (dying->fork_pending_child)
		process_fork_abort_pending_on_exit(dying);
	process_mm_release_on_exit(dying);
	if (g_exit_ops.clear_console_session)
		g_exit_ops.clear_console_session(dying);
	process_release_fds(dying, "EXIT_CLOSE");
}

void process_destroy(process_t *process)
{
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;
	uint64_t kstack = 0;

	if (!process)
		return;

	process_mm_release_on_exit(process);
	if (g_exit_ops.purge_console_waiters)
		g_exit_ops.purge_console_waiters(process);
	if (g_exit_ops.purge_pipe_waiters)
		g_exit_ops.purge_pipe_waiters(process);
	if (g_exit_ops.disarm_clock_wait)
		g_exit_ops.disarm_clock_wait(process);
	process_release_fds(process, "DESTROY");

	if (process->files)
	{
		files_put(process->files);
		process->files = NULL;
	}

	if (process->mode == KERNEL_MODE)
		kstack = process_stack_start(process);
	process_kernel_stack_free(process);

	if (process->mm)
	{
		mm_put(process->mm);
		process->mm = NULL;
	}
	if (process->mode == KERNEL_MODE && kstack &&
	    kstack != INIT_DEBUG_STACK_BASE)
		kfree((void *)(uintptr_t)kstack);

	process_saved_context_clear(process);
	process_saved_environ_clear(process);
	process_saved_cmdline_clear(process);

	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	paging_ir0_mm_checkpoint("destroy-after", (int32_t)process->task.pid);
}
