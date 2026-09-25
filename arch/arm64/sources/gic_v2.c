/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: gic_v2.c
 * Description: Minimal GICv2 bring-up from firmware-provided MMIO resources.
 *
 * Reference: ARM IHI 0048 (GIC architecture); Löwenware / OSDev virt timer notes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "gic_v2.h"

#include <stdint.h>

#define GICD_MIN_SIZE 0x1000UL
#define GICC_MIN_SIZE 0x1000UL

#define GICD_REG32(off) (*(volatile uint32_t *)(uintptr_t)(g_gicd_base + (off)))
#define GICC_REG32(off) (*(volatile uint32_t *)(uintptr_t)(g_gicc_base + (off)))
#define GICD_REG8_PTR(off) ((volatile uint8_t *)(uintptr_t)(g_gicd_base + (off)))
#define GICD_REG32_PTR(off) ((volatile uint32_t *)(uintptr_t)(g_gicd_base + (off)))

#define GICD_CTLR_ENABLE 1U
#define GICC_CTLR_ENABLE 1U

static int g_gic_inited;
static uint64_t g_gicd_base;
static uint64_t g_gicc_base;

int arm64_gic_v2_configure(uint64_t distributor_base, uint64_t distributor_size,
			   uint64_t cpu_base, uint64_t cpu_size)
{
	if (distributor_base == 0U || cpu_base == 0U ||
	    distributor_size < GICD_MIN_SIZE || cpu_size < GICC_MIN_SIZE ||
	    distributor_base + distributor_size < distributor_base ||
	    cpu_base + cpu_size < cpu_base)
		return -1;
	if (g_gic_inited &&
	    (g_gicd_base != distributor_base || g_gicc_base != cpu_base))
		return -1;
	g_gicd_base = distributor_base;
	g_gicc_base = cpu_base;
	return 0;
}

int arm64_gic_v2_init(void)
{
	if (g_gic_inited)
		return 0;
	if (g_gicd_base == 0U || g_gicc_base == 0U)
		return -1;

	GICD_REG32(0x000UL) = 0;
	GICC_REG32(0x000UL) = 0;
	GICC_REG32(0x004UL) = 0xffU;
	GICC_REG32(0x000UL) = GICC_CTLR_ENABLE;
	GICD_REG32(0x000UL) = GICD_CTLR_ENABLE;

	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	g_gic_inited = 1;
	return 0;
}

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

	if (arm64_gic_v2_init() != 0)
		return -1;

	GICD_REG8_PTR(0x400UL)[irq] = 0x00;
	GICD_REG8_PTR(0x800UL)[irq] = 0x01; /* CPU0 */

	/* Edge for PPI timer (ICFGR bit pair). */
	reg = irq / 16U;
	shift = (irq % 16U) * 2U;
	GICD_REG32_PTR(0xC00UL)[reg] =
		(GICD_REG32_PTR(0xC00UL)[reg] & ~(3U << shift)) | (2U << shift);

	bit = 1U << irq;
	GICD_REG32(0x280UL) = bit;
	GICD_REG32(0x100UL) = bit;

	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	return 0;
}

uint32_t arm64_gic_v2_ack(void)
{
	return GICC_REG32(0x00CUL);
}

void arm64_gic_v2_eoi(uint32_t irq)
{
	GICC_REG32(0x010UL) = irq;
	__asm__ volatile("dsb sy" ::: "memory");
}
