/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 interrupt/PIC/IDT query helpers (FASE58J).
 */

#include <ir0/interrupt_diag.h>
#include "pic.h"
#include "io.h"
#include "idt.h"

uint64_t ir0_arch_read_rflags(void)
{
	uint64_t flags;

	__asm__ volatile("pushfq; pop %0" : "=r"(flags));
	return flags;
}

int ir0_arch_irq_masked(uint8_t irq)
{
	uint16_t port;
	uint8_t line;
	uint8_t mask;

	if (irq < 8)
	{
		port = PIC1_DATA;
		line = irq;
	}
	else
	{
		port = PIC2_DATA;
		line = (uint8_t)(irq - 8);
	}

	mask = inb(port);
	return (mask & (uint8_t)(1u << line)) ? 1 : 0;
}

uint64_t ir0_arch_idt_handler(uint8_t vector)
{
	struct idt_entry64 *ent;

	if (vector > 255)
		return 0;

	ent = &idt[vector];
	return (uint64_t)ent->offset_low |
	       ((uint64_t)ent->offset_mid << 16) |
	       ((uint64_t)ent->offset_high << 32);
}
