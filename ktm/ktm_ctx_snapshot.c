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
#include <ir0/serial_io.h>

void ktm_ctx_snapshot(const process_t *p, const char *reason)
{
	if (!p)
	{
		serial_print("[KTM][CTX] reason=");
		serial_print(reason ? reason : "(null)");
		serial_print(" proc=(null)\n");
		return;
	}

	serial_print("[KTM][CTX] reason=");
	serial_print(reason ? reason : "(null)");
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" comm=");
	serial_print(p->comm[0] ? p->comm : "(none)");
	serial_print(" state=");
	serial_print_hex64((uint64_t)p->state);
	serial_print(" irq_saved=");
	serial_print_hex64((uint64_t)p->irq_frame_saved);
	serial_print(" poll_waiter=");
	serial_print_hex64((uint64_t)(uintptr_t)p->poll_waiter);
	serial_print(" poll_resume_arch=");
	serial_print_hex64((uint64_t)p->poll_resume_via_arch);
	serial_print(" wait_status_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)p->wait_status_ptr);
	serial_print(" rip=");
	serial_print_hex64(p->task.rip);
	serial_print(" rsp=");
	serial_print_hex64(p->task.rsp);
	serial_print(" cs=");
	serial_print_hex64((uint64_t)p->task.cs);
	serial_print(" cr3=");
	serial_print_hex64(process_mm_root(p));
	serial_print("\n");
}

void ktm_panic_class_emit(const char *klass)
{
	serial_print("[KTM][PANIC_CLASS] ");
	serial_print(klass ? klass : "UNKNOWN");
	serial_print("\n");
}
