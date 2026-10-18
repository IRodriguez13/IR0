/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cpu.h
 * Description: Simple polymorphic CPU/ISA facades (build selects impl).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Portable names — no arch_ prefix. ISA asm lives in arch backends.
 * Prefer these over legacy arch_* from new/portable call sites.
 *
 * Do not include this from arch_cpu.h: backends declare a local cpuid()
 * with a subleaf argument that conflicts with the portable prototype.
 */

void cpu_relax(void);
void smp_mb(void);

void enable_interrupts(void);
void disable_interrupts(void);

/*
 * Save IRQ state and disable IRQs; restore with irq_restore().
 * Portable critical sections (spinlock, pipe refcount, …).
 */
unsigned long irq_save(void);
void irq_restore(unsigned long flags);

void cpu_halt(void);

/*
 * Logical CPU topology.  UP builds expose the boot CPU through the same
 * contract that scheduler/IRQ code will consume once SMP is enabled.
 * Masks use logical CPU ids; firmware and hardware ids stay arch-private.
 */
unsigned int cpu_id(void);
uint64_t cpu_possible_mask(void);
uint64_t cpu_present_mask(void);
uint64_t cpu_online_mask(void);
uint64_t cpu_active_mask(void);
unsigned int cpu_online_count(void);
int cpu_is_possible(unsigned int cpu);
int cpu_is_present(unsigned int cpu);
int cpu_is_online(unsigned int cpu);
int cpu_is_active(unsigned int cpu);

uint64_t timer_read(void);

uint64_t rdmsr(uint32_t msr);
void wrmsr(uint32_t msr, uint64_t value);
void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
	   uint32_t *edx);

uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);

/* x86 debug registers; no-op / zero on other ISA */
void debug_reg_write(unsigned int regno, uint64_t value);
uint64_t debug_reg_read(unsigned int regno);
