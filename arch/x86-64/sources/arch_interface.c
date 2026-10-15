/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_interface.c
 * Description: x86-64 implementation of the portable architecture interface.
 */

#include "arch_interface.h"
#include <arch/common/arch_portable.h>
#include <ir0/cpu.h>
#include <ir0/early_clock.h>
#include <ir0/oops.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <config.h>
#include <ir0/irq.h>
#include <interrupt/arch/idt.h> /* idt_set_gate64 for syscall int 0x80 */
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

static void *g_arch_boot_params = NULL;

// Detect MinGW-w64 cross-compilation
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

// Implementaciones específicas de arquitectura
void enable_interrupts(void)
{
    #if MINGW_BUILD
        __asm__ __volatile__("sti" ::: "memory");
    #else
        __asm__ volatile("sti");
    #endif
}

void disable_interrupts(void)
{
    #if MINGW_BUILD
        __asm__ __volatile__("cli" ::: "memory");
    #else
        __asm__ volatile("cli");
    #endif
}

uint8_t inb(uint16_t port)
{
    uint8_t result;
    #if MINGW_BUILD
        __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port) : "memory");
    #else
        asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    #endif
    return result;
}

uintptr_t read_fault_address(void)
{
    #if MINGW_BUILD
        // MinGW-w64 requires explicit 64-bit register constraint
        uint64_t addr64;
        __asm__ __volatile__("mov %%cr2, %q0" : "=r"(addr64) : : "memory");
        return (uintptr_t)addr64;
    #else
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #endif
}

arch_addr_t get_fault_address(void)
{
	return (arch_addr_t)read_fault_address();
}

const char *get_arch_name(void)
{
    return "x86-64 (amd64)";
}

/* uname(2) machine field — short Linux-style ISA token. */
const char *get_arch_uname_machine(void)
{
    return "x86_64";
}

const char *boot_entry_name(void)
{
	return "x86_64 bootstrap entry";
}

int early_clock_available(void)
{
	return 1;
}

uint64_t early_clock_read(void)
{
	uint32_t lo;
	uint32_t hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

early_clock_quality_t early_clock_quality(void)
{
	return early_clock_available() ? EARLY_CLOCK_RAW
				       : EARLY_CLOCK_UNAVAILABLE;
}

void outb(uint16_t port, uint8_t value)
{
    #if MINGW_BUILD
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
    #else
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    #endif
}

void io_wait(void)
{
	outb(0x80, 0);
}

uint16_t inw(uint16_t port)
{
	uint16_t ret;

	__asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outw(uint16_t port, uint16_t value)
{
	__asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t inl(uint16_t port)
{
	uint32_t ret;

	__asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outl(uint16_t port, uint32_t value)
{
	__asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

void cpu_relax(void)
{
	__asm__ volatile("pause");
}

void smp_mb(void)
{
	__asm__ volatile("mfence" ::: "memory");
}

uint64_t rdmsr(uint32_t msr)
{
	uint32_t lo, hi;

	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t)hi << 32) | lo;
}

void wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t lo = (uint32_t)value;
	uint32_t hi = (uint32_t)(value >> 32);

	__asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
	   uint32_t *edx)
{
	uint32_t a, b, c, d;

	__asm__ volatile("cpuid"
			 : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
			 : "a"(leaf));
	if (eax)
		*eax = a;
	if (ebx)
		*ebx = b;
	if (ecx)
		*ecx = c;
	if (edx)
		*edx = d;
}

void debug_reg_write(unsigned int regno, uint64_t value)
{
	switch (regno)
	{
	case 6:
		__asm__ volatile("mov %0, %%dr6" :: "r"(value));
		break;
	case 7:
		__asm__ volatile("mov %0, %%dr7" :: "r"(value));
		break;
	default:
		break;
	}
}

uint64_t debug_reg_read(unsigned int regno)
{
	uint64_t v = 0;

	switch (regno)
	{
	case 6:
		__asm__ volatile("mov %%dr6, %0" : "=r"(v));
		break;
	case 7:
		__asm__ volatile("mov %%dr7, %0" : "=r"(v));
		break;
	default:
		break;
	}
	return v;
}

uint64_t timer_read(void)
{
	uint32_t lo, hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

void cpu_wait(void)
{
#if   MINGW_BUILD
    __asm__ __volatile__("hlt" ::: "memory");
#else
    asm volatile("hlt");
#endif
}

void cpu_idle(void)
{
    /*
     * Safe halt: IRQs must be on or hlt never wakes (no IRQ1 → dead TTY
     * after ash blocks and the idle task runs). clock_wait already sti'd
     * before idle; kernel_idle_loop did not — force it here.
     */
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

/**
 * Architecture-specific early initialization wrapper
 * Calls the appropriate architecture implementation
 */
void early_init(void)
{
    /* x86-64 specific early init */
    extern void early_init_x86_64(void);
    early_init_x86_64();
}

/**
 * Architecture-specific interrupt initialization wrapper
 * Calls the appropriate architecture implementation
 */
void interrupt_init(void)
{
    /* x86-64 specific interrupt init */
    extern void interrupt_init_x86_64(void);
    interrupt_init_x86_64();
}

void irq_init(void)
{
    interrupt_init();
}

void boot_irq_unmask(void)
{
	/* PIC lines — x86 only; arm64 uses GIC PPIs via arch_irq_init. */
	irq_unmask_line(0);
	irq_unmask_line(1);
	irq_unmask_line(2);
#if CONFIG_ENABLE_NETWORKING
	{
		int net_irq = net_stack_get_irq_line();

		if (net_irq >= 0 && net_irq < 16)
			irq_unmask_line((unsigned)net_irq);
	}
#endif
#if CONFIG_ENABLE_MOUSE
	irq_unmask_line(12);
#endif
}

void syscall_init(void)
{
    /*
     * IDT gate 0x80: int $0x80 → syscall_entry_asm (debug_bins ABI).
     * MSR LSTAR: syscall insn → syscall_insn_entry_asm (Linux/musl ABI).
     */
    extern void syscall_entry_asm(void);
    extern void syscall_insn_entry_asm(void);

    idt_set_gate64(0x80, (uint64_t)syscall_entry_asm, 0x08, 0xEE, 0);

    {
        /*
         * STAR[47:32] = kernel CS selector (0x08, GDT index 1).
         * STAR[63:48] = user CS GDT byte offset minus 16 (0x18 - 16 = 0x08).
         * sysret: CS = (base+16)|3 = 0x1B, SS = (base+8)|3 = 0x13 (Linux layout).
         * Do not store 0x18 here — that yields CS 0x2B and #GP on syscall return.
         */
        uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x08 << 48);
        uint64_t lstar = (uint64_t)(uintptr_t)syscall_insn_entry_asm;
        uint64_t sfmask = (uint64_t)(1 << 9); /* clear IF */
        uint32_t efer_lo;
        uint32_t efer_hi;

        /*
         * IA32_EFER.SCE: required for the syscall instruction (init_smoke,
         * musl). Boot only enables LME+NXE; enable SCE when syscalls init.
         */
        __asm__ volatile("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(0xC0000080U));
        efer_lo |= 1U;
        __asm__ volatile("wrmsr" : : "c"(0xC0000080U), "a"(efer_lo), "d"(efer_hi) : "memory");

        __asm__ volatile("wrmsr" : : "c"(0xC0000081U), "a"((uint32_t)star), "d"((uint32_t)(star >> 32)) : "memory");
        __asm__ volatile("wrmsr" : : "c"(0xC0000082U), "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)) : "memory");
        __asm__ volatile("wrmsr" : : "c"(0xC0000084U), "a"((uint32_t)sfmask), "d"((uint32_t)(sfmask >> 32)) : "memory");
    }
}

// ===============================================================================
// FUNCIONES DE DIVISIÓN 64-BIT (para resolver referencias indefinidas)
// ===============================================================================

// Implementación simple de división unsigned 64-bit
uint64_t __udivdi3(uint64_t a, uint64_t b)
{
    if (b == 0)
    {
        // División por cero - en un kernel real esto debería panic
        panic("Prohibidísimo dividir por cero. Division by zero detected.");
        return 0;
    }

    uint64_t result = 0;
    uint64_t remainder = 0;

    // Algoritmo de división simple
    for (int i = 63; i >= 0; i--)
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
