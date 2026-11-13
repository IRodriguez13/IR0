/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: irq_dispatch.c
 * Description: Portable IRQ handler table. Architecture backends enable lines.
 */

#include <ir0/errno.h>
#include <ir0/irq.h>
#include <string.h>

#define IR0_IRQ_TABLE 256U

static ir0_irq_fn g_fn[IR0_IRQ_TABLE];
static void *g_data[IR0_IRQ_TABLE];

int irq_request(uint32_t irq, ir0_irq_fn fn, void *data, const char *name)
{
	int rc;

	(void)name;
	if (!fn || irq >= IR0_IRQ_TABLE)
		return -EINVAL;
	if (g_fn[irq])
		return -EBUSY;
	rc = irq_hw_enable(irq);
	if (rc < 0)
		return rc;
	g_fn[irq] = fn;
	g_data[irq] = data;
	return 0;
}

int irq_dispatch(uint32_t irq)
{
	if (irq >= IR0_IRQ_TABLE || !g_fn[irq])
		return -ENOENT;
	g_fn[irq](irq, g_data[irq]);
	return 0;
}
