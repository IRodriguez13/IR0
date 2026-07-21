/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_invariant.c
 * Description: IR0 kernel source — ktm invariant
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>

#ifdef IR0_KERNEL_TESTS

void ktm_invariant_process(const process_t *p, const char *tag)
{
	if (!p)
	{
		klog_debug_fmt("KTM", "[KTM][PROC] tag=%s (null)", tag ? tag : "?");
		return;
	}

	klog_debug_fmt("KTM",
		       "[KTM][PROC] tag=%s pid=%x state=%llx irq_saved=%llx poll_resume_arch=%llx poll_waiter=%llx",
		       tag ? tag : "?", (unsigned)(uint32_t)p->task.pid,
		       (unsigned long long)(uint64_t)p->state,
		       (unsigned long long)(uint64_t)p->irq_frame_saved,
		       (unsigned long long)(uint64_t)p->poll_resume_via_arch,
		       (unsigned long long)(uint64_t)(uintptr_t)p->poll_waiter);
}

#endif /* IR0_KERNEL_TESTS */
