/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: interface.c
 * Description: ARM64 implementation of the portable architecture interface (cpu.h, irq.h).
 *
 * The freestanding image links freestanding_stubs.c for a minimal subset; the
 * portable/product kernel uses this TU instead.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cpu.h>
#include <ir0/irq.h>
#include <ir0/arch_types.h>
#include <ir0/oops.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/common/arch_portable.h>
#include "irq_backend.h"
#include "timer.h"

void *__attribute__((weak)) g_arch_boot_params;

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

arch_addr_t get_fault_address(void)
{
	return (arch_addr_t)read_fault_address();
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

early_clock_quality_t early_clock_quality(void)
{
	return early_clock_available() ? EARLY_CLOCK_RAW : EARLY_CLOCK_UNAVAILABLE;
}

void io_wait(void)
{
}

void cpu_relax(void)
{
	__asm__ volatile("yield" ::: "memory");
}

void smp_mb(void)
{
	__asm__ volatile("dsb ish" ::: "memory");
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

void cpu_halt(void)
{
	cpu_wait();
}

void set_boot_params(void *params)
{
	g_arch_boot_params = params;
}

void *get_boot_params(void)
{
	return g_arch_boot_params;
}

void early_init(void)
{
	extern void early_init_arm64(void);

	early_init_arm64();
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

uint8_t inb(uint16_t port)
{
	(void)port;
	return 0;
}

void outb(uint16_t port, uint8_t value)
{
	(void)port;
	(void)value;
}

uint16_t inw(uint16_t port)
{
	(void)port;
	return 0;
}

void outw(uint16_t port, uint16_t value)
{
	(void)port;
	(void)value;
}

uint32_t inl(uint16_t port)
{
	(void)port;
	return 0;
}

void outl(uint16_t port, uint32_t value)
{
	(void)port;
	(void)value;
}

void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
	   uint32_t *edx)
{
	(void)leaf;
	if (eax)
		*eax = 0;
	if (ebx)
		*ebx = 0;
	if (ecx)
		*ecx = 0;
	if (edx)
		*edx = 0;
}

uint64_t rdmsr(uint32_t msr)
{
	(void)msr;
	return 0;
}

void wrmsr(uint32_t msr, uint64_t value)
{
	(void)msr;
	(void)value;
}

void debug_reg_write(unsigned int regno, uint64_t value)
{
	(void)regno;
	(void)value;
}

uint64_t debug_reg_read(unsigned int regno)
{
	(void)regno;
	return 0;
}

uint8_t mmio_read8(arch_addr_t addr)
{
	return *(volatile uint8_t *)(uintptr_t)addr;
}

void mmio_write8(arch_addr_t addr, uint8_t value)
{
	*(volatile uint8_t *)(uintptr_t)addr = value;
}

uint16_t mmio_read16(arch_addr_t addr)
{
	return *(volatile uint16_t *)(uintptr_t)addr;
}

void mmio_write16(arch_addr_t addr, uint16_t value)
{
	*(volatile uint16_t *)(uintptr_t)addr = value;
}

uint32_t mmio_read32(arch_addr_t addr)
{
	return *(volatile uint32_t *)(uintptr_t)addr;
}

void mmio_write32(arch_addr_t addr, uint32_t value)
{
	*(volatile uint32_t *)(uintptr_t)addr = value;
}

/*
 * libgcc may call these for 64-bit division on targets without native idiv;
 * provide a small non-lifting implementation (mirrors x86 arch_interface).
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
