/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: interface.c
 * Description: ARM64 implementation of the portable architecture interface (cpu.h, irq.h).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cpu.h>
#include <ir0/irq.h>
#include <ir0/arch_types.h>
#include <asm/processor.h>
#include <ir0/oops.h>
#include <ir0/oops_port.h>
#include <ir0/fpu_backend.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/common/arch_portable.h>
#include "irq_backend.h"
#include "timer.h"

/* Implemented by the arm64 IRQ provider. */

void enable_interrupts(void)
{
	unsigned long daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	daif &= ~(1UL << 7);
	__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
}

void disable_interrupts(void)
{
	unsigned long daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	daif |= (1UL << 7);
	__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
}

uintptr_t read_fault_address(void)
{
	uint64_t far;

	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	return (uintptr_t)far;
}

int early_clock_available(void)
{
	uint64_t frq;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	return frq != 0;
}

uint64_t early_clock_read(void)
{
	return timer_read();
}

void smp_mb(void)
{
	__asm__ volatile("dsb ish" ::: "memory");
}

void mb(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

void cpu_sync(void)
{
	__asm__ volatile("isb" ::: "memory");
}

void cpu_wait(void)
{
	__asm__ volatile("wfi" ::: "memory");
}

void cpu_idle(void)
{
	enable_interrupts();
	cpu_wait();
}

/* Terminal halt for panic and failed power-off (<ir0/oops_port.h>). */
void cpu_halt(void)
{
	for (;;)
		cpu_wait();
}

void early_init(void)
{
	extern void early_init_arm64(void);

	early_init_arm64();
	fpu_init();
}

void interrupt_init(void)
{
	irq_tables_init();
	irq_controller_init();
}

void irq_init(void)
{
	interrupt_init();
}

void ir0_boot_port_unmask_irqs(void)
{
	(void)arm64_irq_backend_enable(ARM64_IRQ_PHYS_TIMER);
#if defined(IR0_ARM64_PRODUCT_KERNEL)
	arch_timer_periodic_arm();
#endif
}

void syscall_init(void)
{
	/* SVC from EL0 is handled in exc_early.c; VBAR installed at irq_tables_init(). */
}

/*
 * libgcc may call these for 64-bit division on targets without native idiv;
 * provide a small non-lifting implementation (mirrors x86 interface.c).
 */
uint64_t __udivdi3(uint64_t a, uint64_t b)
{
	uint64_t result = 0;
	uint64_t remainder = 0;
	int i;

	if (b == 0)
	{
		panic("Division by zero in __udivdi3");
		return 0;
	}

	for (i = 63; i >= 0; i--)
	{
		remainder = (remainder << 1) | ((a >> i) & 1);
		if (remainder >= b)
		{
			remainder -= b;
			result |= (1ULL << i);
		}
	}

	return result;
}
