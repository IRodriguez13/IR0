/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal_diag.c
 * Description: Signal trace hook: klog tags and KTM flight records for
 *              kernel/signals.c. Tags consumed by smokes stay stable
 *              (SIGFRAME_ABANDON, USER_FAULT_FRAME ... SIGNAL_KILL_SIGSEGV).
 */

#include "signal_diag.h"
#include <kernel/process.h>
#include <ktm.h>

#ifndef SIGNAL_DELIVER_LOG
#define SIGNAL_DELIVER_LOG 0
#endif

static void trace_deliver_fault(const process_t *p, int sig, uint64_t addr,
				uint64_t ip, uint64_t handler)
{
	uint32_t pid = (uint32_t)p->task.pid;

	if (SIGNAL_DELIVER_LOG)
		klog_info_fmt("SIGNAL",
			      "[SIGNAL][DELIVER] pid=0x%x sig=0x%x cr2=0x%llx "
			      "rip=0x%llx handler=0x%llx",
			      (unsigned)pid, (unsigned)sig,
			      (unsigned long long)addr,
			      (unsigned long long)ip,
			      (unsigned long long)handler);
	KTM_FLIGHT(KTM_FL_PF_USER, pid, (uint32_t)addr, (uint32_t)(addr >> 32),
		   (uint32_t)ip);
	KTM_FLIGHT(KTM_FL_SIGNAL_DELIVER, (uint32_t)sig, pid,
		   (uint32_t)handler, 0);
}

void signal_trace(enum signal_trace_event ev, const process_t *p, int sig,
		  uint64_t a, uint64_t b, uint64_t c)
{
	if (!p)
		return;

	switch (ev)
	{
	case SIGNAL_TRACE_SEND:
		if (IR0_DEBUG_PROC)
			klog_info_fmt("SIGNAL",
				      "[SIGTERM_AUDIT] send_signal pid=0x%x sig=0x%x "
				      "pending=0x%x mask=0x%x state=0x%x",
				      (unsigned)p->task.pid, (unsigned)sig,
				      (unsigned)p->signal_pending,
				      (unsigned)p->signal_mask,
				      (unsigned)p->state);
		break;
	case SIGNAL_TRACE_WAKE:
		if (IR0_DEBUG_PROC)
			klog_info_fmt("SIGNAL",
				      "[SIGTERM_AUDIT] wake pid=0x%x state=READY "
				      "promote=1",
				      (unsigned)p->task.pid);
		break;
	case SIGNAL_TRACE_DELIVER_FAULT:
		trace_deliver_fault(p, sig, a, b, c);
		break;
	case SIGNAL_TRACE_ABANDON:
		klog_info_fmt("SIGNAL",
			      "SIGFRAME_ABANDON pid=%x usp=%llx frame_sp=%llx "
			      "mask_saved=%x",
			      (unsigned)((uint32_t)p->task.pid),
			      (unsigned long long)a,
			      (unsigned long long)b,
			      (unsigned)(p->signal_mask_saved_valid ? 1U : 0U));
		break;
	case SIGNAL_TRACE_KILL_SEGV:
		/*
		 * Pending-bit kill without a live fault frame (kill(2) from
		 * another path): greppable tag for session smokes.
		 */
		klog_info_fmt("FAULT",
			      "USER_FAULT_FRAME CLASSIFY SIGNAL_KILL_SIGSEGV "
			      "pid=%x comm=%s",
			      (unsigned)((uint32_t)p->task.pid),
			      p->comm[0] ? p->comm : "(none)");
		klog_print("USER_FAULT_FRAME\n");
		break;
	}
}
