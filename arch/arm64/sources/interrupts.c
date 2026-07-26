/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: interrupts.c
 * Description: ARM64 interrupt bring-up via portable irq facade (VBAR + GIC).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/irq.h>
#include "gic_v2.h"

void interrupt_init_arm64(void)
{
	irq_tables_init();
	irq_controller_init();
	/* Default freestanding timer PPI — same as early boot path. */
	(void)irq_unmask_line(ARM64_GIC_PPI_PHYS_TIMER);
}
