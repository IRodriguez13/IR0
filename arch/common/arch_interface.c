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
 * Description: IR0 kernel source/header file
 */

#include "arch_interface.h"
#include <arch/common/arch_portable.h>
#include <ir0/oops.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__x86_64__) || defined(__amd64__)
#include <interrupt/arch/idt.h>
#endif
#if defined(__x86_64__) || defined(__amd64__) || defined(__i386__)
#include <config.h>
#include <interrupt/arch/pic.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif
#endif

static void *g_arch_boot_params = NULL;

// Detect MinGW-w64 cross-compilation
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

// Implementaciones específicas de arquitectura
void arch_enable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("sti" ::: "memory");
    #else
        __asm__ volatile("sti");
    #endif
#elif defined(__aarch64__)
    // ARM64: msr daifclr, #2
    __asm__ volatile("msr daifclr, #2" ::: "memory");
#endif
}

void arch_disable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("cli" ::: "memory");
    #else
        __asm__ volatile("cli");
    #endif
#elif defined(__aarch64__)
    // ARM64: msr daifset, #2
    __asm__ volatile("msr daifset, #2" ::: "memory");
#endif
}

uint8_t inb(uint16_t port)
{
#if defined(__x86_64__) || defined(__i386__)
    uint8_t result;
    #if MINGW_BUILD
        __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port) : "memory");
    #else
        asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    #endif
    return result;
#elif defined(__aarch64__)
    // ARM no tiene inb
    return 0;
#endif
}

uintptr_t read_fault_address(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD && defined(__x86_64__)
        // MinGW-w64 requires explicit 64-bit register constraint
        uint64_t addr64;
        __asm__ __volatile__("mov %%cr2, %q0" : "=r"(addr64) : : "memory");
        return (uintptr_t)addr64;
    #elif defined(__x86_64__)
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #else
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #endif
#elif defined(__aarch64__)
    // ARM64: leer FAR_EL1 register
    uint64_t addr;
    asm volatile("mrs %0, far_el1" : "=r"(addr));
    return addr;
#else
    return 0;
#endif
}

const char *arch_get_name(void)
{
#if defined(__x86_64__)
    return "x86-64 (amd64)";
#elif defined(__i386__)
    return "x86-32 (i386)";
#elif defined(__aarch64__)
    return "ARM64 (aarch64)";
#elif defined(__arm__)
    return "ARM32";
#else
    return "Unknown Architecture";
#endif
}

void outb(uint16_t port, uint8_t value)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
    #else
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    #endif
#elif defined(__aarch64__)
    // ARM no tiene puertos I/O - usar MMIO
    // Implementar cuando sea necesario
#endif
}

void cpu_wait(void)
{
#if defined(__aarch64__)
    asm volatile("wfi" ::: "memory");
#elif MINGW_BUILD
    __asm__ __volatile__("hlt" ::: "memory");
#else
    asm volatile("hlt");
#endif
}

void arch_cpu_idle(void)
{
    cpu_wait();
}

void arch_cpu_halt(void)
{
    cpu_wait();
}

void arch_set_boot_params(void *params)
{
    g_arch_boot_params = params;
}

void *arch_get_boot_params(void)
{
    return g_arch_boot_params;
}

/**
 * Architecture-specific early initialization wrapper
 * Calls the appropriate architecture implementation
 */
void arch_early_init(void)
{
#if defined(__x86_64__) || defined(__amd64__)
    /* x86-64 specific early init */
    extern void arch_early_init_x86_64(void);
    arch_early_init_x86_64();
#elif defined(__aarch64__)
    extern void arch_early_init_arm64(void);
    arch_early_init_arm64();
#else
    #error "Unsupported architecture for arch_early_init()"
#endif
}

/**
 * Architecture-specific interrupt initialization wrapper
 * Calls the appropriate architecture implementation
 */
void arch_interrupt_init(void)
{
#if defined(__x86_64__) || defined(__amd64__)
    /* x86-64 specific interrupt init */
    extern void arch_interrupt_init_x86_64(void);
    arch_interrupt_init_x86_64();
#elif defined(__aarch64__)
    extern void arch_interrupt_init_arm64(void);
    arch_interrupt_init_arm64();
#else
    #error "Unsupported architecture for arch_interrupt_init()"
#endif
}

void arch_irq_init(void)
{
    arch_interrupt_init();
}

void arch_boot_irq_unmask(void)
{
#if defined(__x86_64__) || defined(__amd64__) || defined(__i386__)
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    pic_unmask_irq(2);
#if CONFIG_ENABLE_NETWORKING
    {
        int net_irq = net_stack_get_irq_line();

        if (net_irq >= 0 && net_irq < 16)
            pic_unmask_irq((uint8_t)net_irq);
    }
#endif
#if CONFIG_ENABLE_MOUSE
    pic_unmask_irq(12);
#endif
#elif defined(__aarch64__)
    /* ARM64 GIC unmask policy: board-specific bring-up */
#else
    #error "Unsupported architecture for arch_boot_irq_unmask()"
#endif
}

void arch_syscall_init(void)
{
#if defined(__x86_64__) || defined(__amd64__)
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
#endif
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