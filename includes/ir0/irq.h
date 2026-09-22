/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: irq.h
 * Description: Portable IRQ subsystem bring-up (IDT/PIC vs VBAR/GIC backends).
 *
 * Portable / driver code must use this facade — not interrupt/arch headers.
 * x86: arch/x86-64/.../arch_irq_init.c → interrupt/arch (IDT/PIC impl).
 * arm64: arch/arm64/.../arch_irq_init.c → VBAR + gic_v2 (no interrupt/arch).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/arch_types.h>

/*
 * Install CPU exception / IRQ vector table (x86: IDT; arm64: VBAR/GIC path).
 */
void irq_tables_init(void);

void interrupt_init(void);
void irq_init(void);
void boot_irq_unmask(void);

int register_irq(arch_irq_t irq, void (*handler)(void));
int unregister_irq(arch_irq_t irq);
void irq_eoi(arch_irq_t irq);

unsigned long irq_save(void);
void irq_restore(unsigned long flags);
void enable_interrupts(void);
void disable_interrupts(void);

/*
 * Program the interrupt controller (x86: 8259 PIC; arm64: GIC bring-up hook).
 */
void irq_controller_init(void);

/*
 * PS/2 keyboard IRQ registration when the platform has one (no-op on arm64).
 */
void irq_keyboard_init(void);

/* Unmask a platform IRQ line (x86 PIC; arm64 GICv2 SGI/PPI bank 0). */
void irq_unmask_line(unsigned irq);

/* Drain i8042 for keyboard/mouse demux (x86); no-op on arm64. */
void irq_keyboard_poll_ps2(void);
