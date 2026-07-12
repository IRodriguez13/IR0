/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: gic_v2.c
 * Description: Minimal GICv2 bring-up for QEMU virt (GICD 0x08000000 / GICC 0x08010000).
 *
 * Reference: ARM IHI 0048 (GIC architecture); Löwenware / OSDev virt timer notes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "gic_v2.h"

#include <stdint.h>

#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

#define GICD_CTLR        (*(volatile uint32_t *)(GICD_BASE + 0x000UL))
#define GICD_ISENABLER0  (*(volatile uint32_t *)(GICD_BASE + 0x100UL))
#define GICD_ICENABLER0  (*(volatile uint32_t *)(GICD_BASE + 0x180UL))
#define GICD_ICPENDR0    (*(volatile uint32_t *)(GICD_BASE + 0x280UL))
#define GICD_IPRIORITYR  ((volatile uint8_t *)(GICD_BASE + 0x400UL))
#define GICD_ITARGETSR   ((volatile uint8_t *)(GICD_BASE + 0x800UL))
#define GICD_ICFGR       ((volatile uint32_t *)(GICD_BASE + 0xC00UL))

#define GICC_CTLR        (*(volatile uint32_t *)(GICC_BASE + 0x000UL))
#define GICC_PMR         (*(volatile uint32_t *)(GICC_BASE + 0x004UL))
#define GICC_IAR         (*(volatile uint32_t *)(GICC_BASE + 0x00CUL))
#define GICC_EOIR        (*(volatile uint32_t *)(GICC_BASE + 0x010UL))

#define GICD_CTLR_ENABLE 1U
#define GICC_CTLR_ENABLE 1U

int arm64_gic_v2_enable(uint32_t irq)
{
	uint32_t bit;
	uint32_t reg;
	uint32_t shift;

	if (irq >= 32U)
	{
		/* Freestanding slice only programs PPI/SGI bank 0. */
		return -1;
	}

	GICD_CTLR = 0;
	GICC_CTLR = 0;

	/* Priority: highest for timer; PMR accepts anything below 0xff. */
	GICD_IPRIORITYR[irq] = 0x00;
	GICD_ITARGETSR[irq] = 0x01; /* CPU0 */

	/* Edge for PPI timer (ICFGR bit pair). */
	reg = irq / 16U;
	shift = (irq % 16U) * 2U;
	GICD_ICFGR[reg] = (GICD_ICFGR[reg] & ~(3U << shift)) | (2U << shift);

	bit = 1U << irq;
	GICD_ICPENDR0 = bit;
	GICD_ISENABLER0 = bit;

	GICC_PMR = 0xffU;
	GICC_CTLR = GICC_CTLR_ENABLE;
	GICD_CTLR = GICD_CTLR_ENABLE;

	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	return 0;
}

uint32_t arm64_gic_v2_ack(void)
{
	return GICC_IAR;
}

void arm64_gic_v2_eoi(uint32_t irq)
{
	GICC_EOIR = irq;
	__asm__ volatile("dsb sy" ::: "memory");
}
