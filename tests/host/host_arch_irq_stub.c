/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * Host stubs for arch_irq_save / arch_irq_restore (ARCH-5 facade contract).
 * Simulates nested IRQ mask depth without real ISA asm.
 */

#include <stdint.h>

static unsigned long g_irq_depth;
static unsigned long g_saved_stack[8];
static unsigned g_saved_n;

unsigned long arch_irq_save(void)
{
	unsigned long token = g_irq_depth;

	if (g_saved_n < 8)
	{
		g_saved_stack[g_saved_n++] = token;
	}
	g_irq_depth++;
	return token;
}

void arch_irq_restore(unsigned long flags)
{
	(void)flags;
	if (g_irq_depth > 0)
	{
		g_irq_depth--;
	}
	if (g_saved_n > 0)
	{
		g_saved_n--;
	}
}

unsigned long host_arch_irq_depth(void)
{
	return g_irq_depth;
}

void host_arch_irq_reset(void)
{
	g_irq_depth = 0;
	g_saved_n = 0;
}
