/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: d1_16_tty_read_diag.c
 * Description: D1.16 TTY read/wake Linux-like diagnostics
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <d1_16_tty_read_diag.h>
#include <ir0/ash_smoke.h>
#include <ir0/process.h>
#include <ir0/serial_io.h>

static int d1_16_active(struct process *p)
{
	if (!p || !p->comm[0])
		return 0;
	if (p->comm[0] == 's' && p->comm[1] == 'h' && p->comm[2] == '\0')
		return 1;
	return ir0_ash_smoke_active();
}

void d1_16_tty_read_pre(struct process *p, int fd, int nonblock,
			uintptr_t tty_ptr)
{
	if (!d1_16_active(p))
		return;

	serial_print("[D1.16][read_pre] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" tty=");
	serial_print_hex64((uint64_t)tty_ptr);
	serial_print(" nonblock=");
	serial_print_hex64((uint64_t)nonblock);
	serial_print("\n");
}

void d1_16_tty_read_block(struct process *p, int waiters, const char *reason)
{
	if (!d1_16_active(p))
		return;

	serial_print("[D1.16][read_block] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" waiters=");
	serial_print_hex64((uint64_t)waiters);
	serial_print(" reason=");
	serial_print(reason ? reason : "?");
	serial_print("\n");
}

void d1_16_tty_line_ready(uintptr_t tty_ptr, size_t queued)
{
	if (!ir0_ash_smoke_active())
		return;

	serial_print("[D1.16][tty_line_ready] tty=");
	serial_print_hex64((uint64_t)tty_ptr);
	serial_print(" queued=");
	serial_print_hex64((uint64_t)queued);
	serial_print("\n");
}

void d1_16_tty_wake(int waiters_before, int woke, uint32_t pid)
{
	if (!ir0_ash_smoke_active() && waiters_before == 0)
		return;

	serial_print("[D1.16][tty_wake] waiters=");
	serial_print_hex64((uint64_t)waiters_before);
	serial_print(" woke=");
	serial_print_hex64((uint64_t)woke);
	serial_print(" pid=");
	serial_print_hex32(pid);
	serial_print("\n");
}

void d1_16_tty_read_resume(struct process *p, const char *reason)
{
	if (!d1_16_active(p))
		return;

	serial_print("[D1.16][read_resume] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" reason=");
	serial_print(reason ? reason : "?");
	serial_print("\n");
}

void d1_16_tty_state_transition(struct process *p, int from_state,
				int to_state)
{
	if (!d1_16_active(p))
		return;

	serial_print("[D1.16][sched_state] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" from=");
	serial_print_hex64((uint64_t)from_state);
	serial_print(" to=");
	serial_print_hex64((uint64_t)to_state);
	serial_print("\n");
}
