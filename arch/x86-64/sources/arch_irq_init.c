/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_irq_init.c
 * Description: x86-64 irq_* facade → IDT / PIC / keyboard.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/irq.h>
#include <interrupt/arch/idt.h>
#include <interrupt/arch/pic.h>
#include <interrupt/arch/keyboard.h>

void irq_tables_init(void)
{
	idt_init64();
	idt_load64();
}

void irq_controller_init(void)
{
	pic_remap64();
}

void irq_keyboard_init(void)
{
	keyboard_init();
}

void irq_unmask_line(unsigned irq)
{
	if (irq < 16)
		pic_unmask_irq((uint8_t)irq);
}

void irq_keyboard_poll_ps2(void)
{
	keyboard_poll_ps2();
}
