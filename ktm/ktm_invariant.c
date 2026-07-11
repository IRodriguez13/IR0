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
#include <ir0/serial_io.h>

#ifdef IR0_KERNEL_TESTS

void ktm_invariant_process(const process_t *p, const char *tag)
{
	if (!p)
	{
		serial_print("[KTM][PROC] tag=");
		serial_print(tag ? tag : "?");
		serial_print(" (null)\n");
		return;
	}

	serial_print("[KTM][PROC] tag=");
	serial_print(tag ? tag : "?");
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" state=");
	serial_print_hex64((uint64_t)p->state);
	serial_print(" irq_saved=");
	serial_print_hex64((uint64_t)p->irq_frame_saved);
	serial_print(" poll_resume_arch=");
	serial_print_hex64((uint64_t)p->poll_resume_via_arch);
	serial_print(" poll_waiter=");
	serial_print_hex64((uint64_t)(uintptr_t)p->poll_waiter);
	serial_print("\n");
}

#endif /* IR0_KERNEL_TESTS */
