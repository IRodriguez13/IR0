/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_irq_init.c
 * Description: ARM64 irq_* facade → VBAR + GICv2 (no interrupt/arch).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/irq.h>
#include <stdint.h>

#include "exc_early.h"
#include "gic_v2.h"

void irq_tables_init(void)
{
	(void)arm64_vbar_early_install();
}

void irq_controller_init(void)
{
	(void)arm64_gic_v2_init();
}

void irq_keyboard_init(void)
{
	/* No PS/2 on virt/rpi path yet. */
}

void irq_unmask_line(unsigned irq)
{
	(void)arm64_gic_v2_enable((uint32_t)irq);
}

void irq_keyboard_poll_ps2(void)
{
}
