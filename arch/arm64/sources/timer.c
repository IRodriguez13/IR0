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
#include <config.h>
#include <ir0/boot_log.h>
#include <ir0/clock.h>
#include <ir0/delay.h>
#include <ir0/irq.h>
#include "irq_backend.h"
#include "timer.h"

static uint64_t g_cntfrq;
static uint32_t g_hz;
static volatile int g_timer_irq_seen;

int arm64_timer_irq_seen(void)
{
	return g_timer_irq_seen;
}

static void arch_timer_irq(uint32_t irq, void *data)
{
	(void)irq;
	(void)data;
	if (!g_timer_irq_seen)
	{
		g_timer_irq_seen = 1;
		ir0_boot_smoke("ARM64_TIMER_IRQ_OK");
	}

	if (clock_is_ready())
	{
		clock_note_irq();
		clock_tick();
		arch_timer_periodic_arm();
	}
}

/*
 * Runs at platform setup, before the boot IRQ demo, and again from
 * clock_timer_init(), where the line is already taken (-EBUSY).
 */
void timer_init(void)
{
	uint64_t frq;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	g_cntfrq = frq;
	g_hz = (uint32_t)frq;
	(void)irq_request(ARM64_IRQ_PHYS_TIMER, arch_timer_irq, NULL,
			  "arch_timer");
}

/* The tick IRQ is armed later by arch_timer_periodic_arm(); no PC timer runs. */
clock_timer_t clock_timer_init(void)
{
	timer_init();
	return CLOCK_TIMER_NONE;
}

uint64_t timer_read(void)
{
	uint64_t pct;

	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(pct));
	return pct;
}

/* Generic-timer delay: CNTPCT_EL0 runs at CNTFRQ_EL0 on every core. */
void udelay(unsigned long us)
{
	uint64_t frq = g_cntfrq;
	uint64_t start;
	uint64_t ticks;

	if (frq == 0)
		__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	ticks = ((uint64_t)us * frq + 999999U) / 1000000U;
	start = timer_read();
	while (timer_read() - start < ticks)
		;
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

void arch_timer_periodic_arm(void)
{
	uint64_t frq = g_cntfrq;
	uint64_t tval;
	uint64_t ctl = 1UL; /* ENABLE=1, interrupt unmasked */

	if (frq == 0)
		__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	if (frq == 0)
		return;

	tval = frq / (uint64_t)CONFIG_TICK_RATE_HZ;
	if (tval == 0)
		tval = 1;

	arch_timer_oneshot_disarm();
	__asm__ volatile("msr cntp_tval_el0, %0" :: "r"(tval) : "memory");
	__asm__ volatile("isb" ::: "memory");
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(ctl) : "memory");
	__asm__ volatile("isb" ::: "memory");
}
