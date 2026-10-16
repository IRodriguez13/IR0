/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Product owners for process_exit(), matching Linux do_exit() subsystems.
 */

#include "process_internal.h"
#include <ir0/copy_user.h>
#include <ir0/futex.h>
#include <ir0/process_exit.h>
#include <ir0/signals.h>

#if CONFIG_ENABLE_NETWORKING
void tcp_wire_on_process_exit(uint32_t pid);
#endif

static void product_exit_robust_list(process_t *dying)
{
	process_exit_robust_list(dying);
}

static void product_clear_child_tid(process_t *dying)
{
	int zero = 0;
	int *tidptr;

	if (!dying->set_tid_ptr)
		return;
	tidptr = dying->set_tid_ptr;
	dying->set_tid_ptr = NULL;
	if (dying->mode == USER_MODE)
	{
		if (copy_to_user_in_mm(process_pgd(dying),
				       dying == current_process, tidptr, &zero,
				       sizeof(zero)) == 0)
			(void)ir0_futex_wake(tidptr, 1);
	}
	else if (process_validate_userspace_buffer(tidptr, sizeof(zero)) == 0)
	{
		(void)copy_to_user(tidptr, &zero, sizeof(zero));
		(void)ir0_futex_wake(tidptr, 1);
	}
}

static void product_purge_console(process_t *dying)
{
	ir0_console_purge_waiters_for_process(dying);
}

static void product_purge_pipe(process_t *dying)
{
	pipe_purge_waiters_for_process(dying);
}

static void product_purge_ipc(process_t *dying)
{
	ipc_purge_waiters_for_process(dying);
}

static void product_clear_console_session(process_t *dying)
{
	if (dying->pgid > 1)
		ir0_console_clear_fg_pgid((int32_t)dying->pgid,
					 (int32_t)dying->task.pid);
	if ((pid_t)dying->task.pid == dying->sid)
	{
		if (ir0_console_has_ctty_for_sid((int32_t)dying->sid))
		{
			ir0_console_flush_input_session();
			ir0_console_reset_cooked_echo();
		}
		ir0_console_clear_ctty_session((int32_t)dying->sid);
	}
}

static void product_disarm_clock(process_t *dying)
{
	ir0_clock_wait_disarm(dying);
}

static void product_tcp_on_exit(process_t *dying)
{
#if CONFIG_ENABLE_NETWORKING
	tcp_wire_on_process_exit((uint32_t)dying->task.pid);
#else
	(void)dying;
#endif
}

static int product_notify_signal(int pid, int sig)
{
	return send_signal(pid, sig);
}

void process_exit_bind_product(void)
{
	static const struct process_exit_services ops = {
		.exit_robust_list = product_exit_robust_list,
		.clear_child_tid = product_clear_child_tid,
		.purge_console_waiters = product_purge_console,
		.purge_pipe_waiters = product_purge_pipe,
		.purge_ipc_waiters = product_purge_ipc,
		.clear_console_session = product_clear_console_session,
		.disarm_clock_wait = product_disarm_clock,
		.tcp_on_exit = product_tcp_on_exit,
		.notify_signal = product_notify_signal,
	};

	process_exit_bind_services(&ops);
}
