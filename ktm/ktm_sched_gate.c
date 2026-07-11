/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_sched_gate.c
 * Description: IR0 kernel source — ktm sched gate
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/serial_io.h>

#ifdef IR0_KERNEL_TESTS

static volatile unsigned int ktm_irq_nest;

void ktm_sched_gate_enter_irq(void)
{
	ktm_irq_nest++;
}

void ktm_sched_gate_leave_irq(void)
{
	if (ktm_irq_nest > 0)
		ktm_irq_nest--;
}

void ktm_sched_gate_check_before_sched(const char *caller)
{
	if (ktm_irq_nest == 0)
		return;

	serial_print("[KTM][SCHED_GATE] sched from IRQ without contract caller=");
	serial_print(caller ? caller : "?");
	serial_print("\n");
	ktm_fail_at("SCHED_GATE", "sched_schedule_next from IRQ", KTM_FILE, __LINE__);
}

#endif /* IR0_KERNEL_TESTS */
