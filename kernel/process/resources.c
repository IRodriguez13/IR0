/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Process resource lifetime facade.  Process policy publishes lifecycle
 * transitions; this file composes subsystem cleanup in dependency order.
 */

#include "process_internal.h"
#include <ir0/copy_user.h>
#include <ir0/futex.h>

#if CONFIG_ENABLE_NETWORKING
void tcp_wire_on_process_exit(uint32_t pid);
#endif

static void process_clear_child_tid(process_t *dying)
{
	int zero = 0;
	int *tidptr;

	if (!dying->set_tid_ptr)
		return;

	tidptr = dying->set_tid_ptr;
	dying->set_tid_ptr = NULL;
	if (dying->mode == USER_MODE) {
		if (copy_to_user_in_mm(process_pgd(dying),
				       dying == current_process, tidptr, &zero,
				       sizeof(zero)) == 0)
			(void)ir0_futex_wake(tidptr, 1);
	} else if (process_validate_userspace_buffer(tidptr, sizeof(zero)) == 0) {
		(void)copy_to_user(tidptr, &zero, sizeof(zero));
		(void)ir0_futex_wake(tidptr, 1);
	}
}

void process_resources_release_on_exit(process_t *dying)
{
	if (!dying)
		return;

#if CONFIG_ENABLE_NETWORKING
	tcp_wire_on_process_exit((uint32_t)dying->task.pid);
#endif
	process_exit_robust_list(dying);
	process_clear_child_tid(dying);

	/* Wait registrations must not retain a process after zombification. */
	ir0_console_purge_waiters_for_process(dying);
	pipe_purge_waiters_for_process(dying);
	ipc_purge_waiters_for_process(dying);
	if (dying->fork_pending_child)
		process_fork_abort_pending_on_exit(dying);
	process_mm_release_on_exit(dying);

	if (dying->pgid > 1)
		ir0_console_clear_fg_pgid((int32_t)dying->pgid,
					 (int32_t)dying->task.pid);
	if ((pid_t)dying->task.pid == dying->sid) {
		if (ir0_console_has_ctty_for_sid((int32_t)dying->sid)) {
			ir0_console_flush_input_session();
			ir0_console_reset_cooked_echo();
		}
		ir0_console_clear_ctty_session((int32_t)dying->sid);
	}

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

	/* All operations below are idempotent for spawn-fail/reap paths. */
	process_mm_release_on_exit(process);
	ir0_console_purge_waiters_for_process(process);
	pipe_purge_waiters_for_process(process);
	ir0_clock_wait_disarm(process);
	process_release_fds(process, "DESTROY");

	if (process->files) {
		files_put(process->files);
		process->files = NULL;
	}

	if (process->mode == KERNEL_MODE)
		kstack = process_stack_start(process);
	process_kernel_stack_free(process);

	if (process->mm) {
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
