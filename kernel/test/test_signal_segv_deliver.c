/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_signal_segv_deliver.c
 * Description: ktest — SIGSEGV delivery from synthetic IRQ frame
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include <config.h>
#include <ir0/signals.h>
#include <ir0/signal_irq.h>
#include <ir0/process.h>
#include <ir0/kmem.h>
#include <ir0/task_ops.h>
#include <stdint.h>

void ktest_signal_segv_deliver_irq_frame(void)
{
	uint64_t stub[32];
	uint64_t *frame;
	void *handler = (void *)(uintptr_t)0x00450000UL;
	uint64_t fault_addr = 0x08004000UL;
	int delivered;
	process_t *p;
	task_t saved_task;
	process_mode_t saved_mode;
	void (*saved_handler)(int);
	void (*saved_restorer)(void);
	struct sigcontext *saved_context;
	uint32_t saved_sa_flags;
	uint32_t saved_sa_mask;
	uint32_t saved_mask;
	uint32_t saved_ignored;
	uint32_t saved_pending;
	uint32_t saved_mask_saved;
	uint8_t saved_mask_saved_valid;
	uint8_t saved_enter_pending;
	uint8_t saved_last_delivered;
	uint64_t saved_frame_sp;

	KTEST_BEGIN("signal_segv_deliver_irq_frame");

	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	p = current_process;

	/*
	 * This test drives the real delivery path, which mutates process and
	 * task state. Snapshot everything it can touch so the rest of the
	 * suite keeps running as a kernel-mode holder process.
	 */
	saved_task = p->task;
	saved_mode = p->mode;
	saved_handler = p->signal_handlers[SIGSEGV];
	saved_restorer = p->signal_restorer[SIGSEGV];
	saved_context = process_saved_context_peek(p);
	saved_sa_flags = p->signal_sa_flags[SIGSEGV];
	saved_sa_mask = p->signal_sa_mask[SIGSEGV];
	saved_mask = p->signal_mask;
	saved_ignored = p->signal_ignored;
	saved_pending = p->signal_pending;
	saved_mask_saved = p->signal_mask_saved;
	saved_mask_saved_valid = p->signal_mask_saved_valid;
	saved_enter_pending = process_signal_enter_pending(p) != 0;
	saved_last_delivered = p->signal_last_delivered;
	saved_frame_sp = p->signal_frame_sp;

	frame = stub + 15;

	p->mode = USER_MODE;
	p->signal_handlers[SIGSEGV] = (void (*)(int))handler;
	p->signal_sa_flags[SIGSEGV] = 0;
	p->signal_sa_mask[SIGSEGV] = 0;
	p->signal_restorer[SIGSEGV] = NULL;
	p->signal_mask = 0;
	p->signal_ignored = 0;
	p->signal_pending = 0;
	p->signal_mask_saved = 0;
	p->signal_mask_saved_valid = 0;
	p->signal_frame_sp = 0;
	process_saved_context_init(p);

	frame[0] = 14;
	frame[1] = 4;
	frame[2] = 0x004422E3UL;
	frame[3] = (uint64_t)USER_CODE_SEL;
	frame[4] = task_initial_user_status();
	frame[5] = 0x7FFFEB38UL;
	frame[6] = (uint64_t)USER_DATA_SEL;
	frame[-7] = 0;
	frame[-6] = 0;
	frame[-3] = 0;
	frame[-1] = 0x11111111UL;

	KASSERT(signals_has_user_handler(p, SIGSEGV));

	delivered = signals_deliver_from_irq_frame(p, SIGSEGV, frame,
						   fault_addr);
	KASSERT_EQ(delivered, 1);
	KASSERT(frame[2] == (uint64_t)(uintptr_t)handler);
	KASSERT(frame[-7] == (uint64_t)SIGSEGV);
	KASSERT(process_saved_context_present(p));
	KASSERT(sigcontext_ip(process_saved_context_peek(p)) ==
		0x004422E3UL);

	process_saved_context_clear(p);

	p->task = saved_task;
	p->mode = saved_mode;
	p->signal_handlers[SIGSEGV] = saved_handler;
	p->signal_restorer[SIGSEGV] = saved_restorer;
	p->signal_sa_flags[SIGSEGV] = saved_sa_flags;
	p->signal_sa_mask[SIGSEGV] = saved_sa_mask;
	p->signal_mask = saved_mask;
	p->signal_ignored = saved_ignored;
	p->signal_pending = saved_pending;
	p->signal_mask_saved = saved_mask_saved;
	p->signal_mask_saved_valid = saved_mask_saved_valid;
	if (saved_enter_pending)
		process_signal_enter_pending_set(p);
	else
		process_signal_enter_pending_clear(p);
	p->signal_last_delivered = saved_last_delivered;
	p->signal_frame_sp = saved_frame_sp;
	if (saved_context)
		KASSERT(process_saved_context_attach(p, saved_context) == 0);

	KTEST_END();
}
