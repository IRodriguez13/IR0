/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: timer.c
 * Description: ARM64 generic timer (CNTFRQ_EL0 / CNTPCT_EL0) for QEMU virt.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

static uint64_t g_cntfrq;
static uint32_t g_hz;

void timer_init(void)
{
	uint64_t frq;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	g_cntfrq = frq;
	g_hz = (uint32_t)frq;
}

uint64_t timer_read(void)
{
	uint64_t pct;

	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(pct));
	return pct;
}

void timer_set_frequency(uint32_t hz)
{
	g_hz = hz;
}

uint32_t timer_get_frequency(void)
{
	if (g_hz != 0)
	{
		return g_hz;
	}
	return (uint32_t)g_cntfrq;
}

/** Returns 0 if CNTFRQ looks sane for smoke tag ARM64_TIMER_OK. */
int arch_timer_smoke_ok(void)
{
	if (g_cntfrq == 0)
	{
		__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(g_cntfrq));
	}
	return g_cntfrq != 0 ? 0 : -1;
}

void arch_timer_oneshot_disarm(void)
{
	uint64_t ctl = 0;

	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(ctl) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void arch_timer_oneshot_arm(uint32_t ticks)
{
	uint64_t tval = ticks ? ticks : 1000U;
	uint64_t ctl = 1UL; /* ENABLE=1, IMASK=0 */

	arch_timer_oneshot_disarm();
	__asm__ volatile("msr cntp_tval_el0, %0" :: "r"(tval) : "memory");
	__asm__ volatile("isb" ::: "memory");
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(ctl) : "memory");
	__asm__ volatile("isb" ::: "memory");
}
