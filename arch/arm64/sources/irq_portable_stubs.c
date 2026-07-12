/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: irq_portable_stubs.c
 * Description: ARM64 stand-in for x86 INTERRUPT_OBJS (idt/pic/isr_stubs).
 *              Real IRQ delivery uses GIC (gic_v2.c) in the freestanding image.
 *              Keeps ARCH=arm64 INTERRUPT_OBJS free of lidt / isr_stubs_64.
 */

void irq_portable_stubs_init(void)
{
	/* GIC bring-up lives in gic_v2 / boot_stub — non-empty TU for the .o. */
}
