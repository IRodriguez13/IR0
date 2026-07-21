/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_ctx_snapshot.c
 * Description: IR0 kernel source — ktm ctx snapshot
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>

void ktm_ctx_snapshot(const process_t *p, const char *reason)
{
	if (!p)
	{
		klog_debug_fmt("KTM", "[KTM][CTX] reason=%s proc=(null)",
			       reason ? reason : "(null)");
		return;
	}

	klog_debug_fmt("KTM",
		       "[KTM][CTX] reason=%s pid=%x comm=%s state=%llx irq_saved=%llx poll_waiter=%llx poll_resume_arch=%llx wait_status_ptr=%llx rip=%llx rsp=%llx cs=%llx cr3=%llx",
		       reason ? reason : "(null)",
		       (unsigned)(uint32_t)p->task.pid,
		       p->comm[0] ? p->comm : "(none)",
		       (unsigned long long)(uint64_t)p->state,
		       (unsigned long long)(uint64_t)p->irq_frame_saved,
		       (unsigned long long)(uint64_t)(uintptr_t)p->poll_waiter,
		       (unsigned long long)(uint64_t)p->poll_resume_via_arch,
		       (unsigned long long)(uint64_t)(uintptr_t)p->wait_status_ptr,
		       (unsigned long long)p->task.rip,
		       (unsigned long long)p->task.rsp,
		       (unsigned long long)(uint64_t)p->task.cs,
		       (unsigned long long)process_mm_root(p));
}

void ktm_panic_class_emit(const char *klass)
{
	klog_debug_fmt("KTM", "[KTM][PANIC_CLASS] %s", klass ? klass : "UNKNOWN");
}
