/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_resched.c
 * Description: Reschedule helpers shared by console wake and idle poll.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sched.h>
#include <ir0/console.h>
#include <ir0/process.h>
#include <ir0/arch_port.h>
#include <ir0/clock.h>
#include <ktm.h>

extern void kernel_idle_poll(void);

static volatile int sched_skip_prev_save;

#define SCHED_VEC_TIMER_IRQ 32U

static int sched_irq_may_preempt(uint64_t *gpr_stack)
{
	uint64_t vec;

	if (!gpr_stack)
		return 0;

	vec = gpr_stack[15];
	if (vec < 32 || vec > 47)
		return 0;

	/* Peek only — do not clear until we actually schedule. */
	if (ir0_console_resched_pending())
		return 2;

	/*
	 * Keyboard/stdin wake sets clock_request_sched_resched without always
	 * arming tty_need_resched (stdin_waiters-only). Honor it on any IRQ.
	 */
	if (clock_sched_resched_pending_peek() && sched_count_runnable() > 1)
		return 4;

	/*
	 * Brief PIT fairness window after TTY wake (see ir0_console_note_tty_wake).
	 * Avoids global timer preempt during PID1 fork/exec boot.
	 */
	if (vec == SCHED_VEC_TIMER_IRQ && ir0_console_timer_resched_pending() &&
	    sched_count_runnable() > 1)
		return 3;

	return 0;
}

void sched_try_preempt_blocked(void)
{
	if (!current_process || current_process->state != PROCESS_BLOCKED)
		return;
	if (sched_count_runnable() == 0)
		return;
	sched_schedule_next();
}

int sched_user_return_take_switch(void)
{
	if (!current_process || current_process->state == PROCESS_BLOCKED)
		return 0;
	if (sched_count_runnable() <= 1)
		return 0;
	if (!clock_take_sched_resched_pending())
		return 0;
	return 1;
}

void sched_need_resched_user_return(void)
{
	if (sched_user_return_take_switch())
		sched_schedule_next();
}

void sched_context_switch_skip_prev_save(void)
{
	sched_skip_prev_save = 1;
}

int sched_context_switch_take_skip_prev_save(void)
{
	int skip = sched_skip_prev_save;

	sched_skip_prev_save = 0;
	return skip;
}

int sched_irq_preempt_from_frame(uint64_t *gpr_stack)
{
	int why;

	why = sched_irq_may_preempt(gpr_stack);
	if (!why)
		return 0;
	if (gpr_stack[15] < 32 || gpr_stack[15] > 47)
		return 0;

	/*
	 * TTY wake: reader marked READY. Old code called take_resched() then
	 * aborted when current was BLOCKED, clearing the flag so ash never
	 * ran after login. If we are inside tty_sleep on this stack, leave
	 * the flag for idle_poll and let the sleeper see READY. Otherwise
	 * schedule the woken reader.
	 */
	if (why == 2)
	{
		if (ir0_console_in_tty_sleep())
			return 0;

		(void)ir0_console_take_resched();
		KTM_EVENT(KTM_EV_SCHED_IRQ_TTY_PREEMPT);
		/*
		 * Ring-0 interrupt context (idle / kernel): never schedule here.
		 * Mid-ISR switch_to corrupts prev RIP/RSP → #UD into BSS on resume.
		 * Wake left tasks READY; idle_poll / cooperative yield picks them up.
		 */
		if ((gpr_stack[18] & 3U) != 3U)
			return 0;
		if (sched_count_runnable() <= 1)
			return 0;
		process_save_user_context_from_irq_frame(gpr_stack);
		sched_context_switch_skip_prev_save();
		sched_schedule_next();
		return 0;
	}

	if (!current_process || current_process->state == PROCESS_BLOCKED)
		return 0;
	if (sched_count_runnable() <= 1)
		return 0;
	if ((gpr_stack[18] & 3U) != 3U)
		return 0;

	if (why == 3)
		KTM_EVENT(KTM_EV_SCHED_IRQ_TIMER_PREEMPT);
	if (why == 4)
		(void)clock_take_sched_resched_pending();

	process_save_user_context_from_irq_frame(gpr_stack);
	sched_context_switch_skip_prev_save();
#if defined(IR0_KERNEL_TESTS)
	KTM_TRACE("sched_irq_preempt tty_wake");
#endif
	sched_schedule_next();
	return 0;
}
