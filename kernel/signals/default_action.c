/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: default_action.c
 * Description: Default-fatal signal delivery into process exit/zombie path.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/signals.h>
#include <ir0/ktm/klog.h>

int process_signal_is_default_fatal(process_t *p, int sig)
{
	void (*handler)(int);

	if (!p)
		return 0;
	if (sig == SIGKILL)
		return 1;
	/*
	 * Signals whose default action is Term or Core in signal(7) and that
	 * are killed here at send time when neither caught, ignored nor
	 * blocked. The task ends WIFSIGNALED with that signal, as on Linux;
	 * SIGPIPE is the one write(2) raises on a pipe or FIFO with no reader
	 * (pipe(7)). The remaining Term signals are applied by get_signal().
	 */
	if (sig != SIGTERM && sig != SIGHUP && sig != SIGINT && sig != SIGQUIT &&
	    sig != SIGUSR1 && sig != SIGUSR2 && sig != SIGSEGV && sig != SIGFPE &&
	    sig != SIGILL && sig != SIGBUS && sig != SIGABRT && sig != SIGALRM &&
	    sig != SIGPIPE)
		return 0;
	if (p->signal_ignored & SIGNAL_MASK(sig))
		return 0;
	/* A blocked signal stays pending (Linux): not fatal until unblocked. */
	if (p->signal_mask & SIGNAL_MASK(sig))
		return 0;
	handler = p->signal_handlers[sig];
	if (handler && handler != SIG_DFL && handler != SIG_IGN)
		return 0;
	return 1;
}

void signal_exit_current(int sig)
{
	process_t *self = current_process;

	self->signal_pending &= ~SIGNAL_MASK(sig);
	self->exit_signal = sig;
	process_exit(0);
}

int process_signal_default_kill(process_t *dying, int sig)
{
	if (!dying || dying->state == PROCESS_ZOMBIE)
		return 0;
	if (!process_signal_is_default_fatal(dying, sig))
		return 0;

	/*
	 * Self-terminate must use process_exit(): publish the zombie and schedule
	 * away. A dead task must never reach an ISA userspace-return path.
	 */
	if (dying == current_process)
		signal_exit_current(sig);

	process_pre_zombie_teardown(dying);

	dying->signal_pending &= ~SIGNAL_MASK(sig);
	dying->exit_signal = sig;
	process_lifecycle_publish_exit(dying, 0);
	sched_remove(dying);

#if IR0_DEBUG_PROC
	klog_debug_fmt("SIGNAL", "[SIGTERM_AUDIT] process_signal_default_kill pid=%x sig=%x wait_status=%x", (unsigned)((uint32_t)dying->task.pid), (unsigned)((uint32_t)sig), (unsigned)((uint32_t)process_child_wait_status_word(dying)));
#endif

	process_notify_parent_of_exit(dying);

	return 1;
}
