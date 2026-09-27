/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bcm2836_irq.c
 * Description: BCM2836 local interrupt controller for Raspberry Pi 2/3.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "bcm2836_irq.h"

#include <stdint.h>

#define BCM2836_LOCAL_MIN_SIZE          0x70UL
#define BCM2836_CORE0_TIMER_IRQ_CONTROL 0x40UL
#define BCM2836_CORE0_IRQ_SOURCE        0x60UL
#define BCM2836_CNTPNSIRQ_BIT           (1U << 1)
#define BCM2836_ARM_PHYS_TIMER_IRQ      30U
#define ARM64_IRQ_SPURIOUS              1023U

static uint64_t g_local_base;

static volatile uint32_t *local_reg(uint64_t offset)
{
	return (volatile uint32_t *)(uintptr_t)(g_local_base + offset);
}

int arm64_bcm2836_irq_configure(uint64_t base, uint64_t size)
{
	if (base == 0U || size < BCM2836_LOCAL_MIN_SIZE || base + size < base)
		return -1;
	if (g_local_base != 0U && g_local_base != base)
		return -1;
	g_local_base = base;
	return 0;
}

int arm64_bcm2836_irq_init(void)
{
	return g_local_base != 0U ? 0 : -1;
}

int arm64_bcm2836_irq_enable(uint32_t irq)
{
	if (irq != BCM2836_ARM_PHYS_TIMER_IRQ || arm64_bcm2836_irq_init() != 0)
		return -1;

	*local_reg(BCM2836_CORE0_TIMER_IRQ_CONTROL) = BCM2836_CNTPNSIRQ_BIT;
	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	return 0;
}

uint32_t arm64_bcm2836_irq_ack(void)
{
	uint32_t source;

	if (g_local_base == 0U)
		return ARM64_IRQ_SPURIOUS;
	source = *local_reg(BCM2836_CORE0_IRQ_SOURCE);
	if ((source & BCM2836_CNTPNSIRQ_BIT) != 0U)
		return BCM2836_ARM_PHYS_TIMER_IRQ;
	return ARM64_IRQ_SPURIOUS;
}

void arm64_bcm2836_irq_eoi(uint32_t token)
{
	(void)token;
	/* The architected timer deasserts when CNTP_CTL_EL0 is disabled. */
}
