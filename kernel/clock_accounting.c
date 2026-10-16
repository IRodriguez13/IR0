/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * File: clock_accounting.c
 * Description: Architecture-neutral IRQ and context-switch counters.
 */

#include <stdint.h>
#include <ir0/clock.h>

static uint64_t clock_irq_total;
static uint64_t clock_ctxt_total;

void clock_note_irq(void)
{
	clock_irq_total++;
}

void clock_note_context_switch(void)
{
	clock_ctxt_total++;
}

uint64_t clock_get_irq_count(void)
{
	return clock_irq_total;
}

uint64_t clock_get_context_switch_count(void)
{
	return clock_ctxt_total;
}
