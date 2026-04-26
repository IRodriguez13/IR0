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
#include <string.h>

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

/**
 * Execute CPUID instruction
 * @leaf: CPUID leaf (eax input)
 * @subleaf: CPUID subleaf (ecx input)
 * @eax: Output for EAX register
 * @ebx: Output for EBX register
 * @ecx: Output for ECX register
 * @edx: Output for EDX register
 */
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("cpuid"
                            : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                            : "a"(leaf), "c"(subleaf)
                            : "memory");
    #else
        asm volatile("cpuid"
                    : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                    : "a"(leaf), "c"(subleaf));
    #endif
#else
    /* Non-x86 architectures */
    if (eax) *eax = 0;
    if (ebx) *ebx = 0;
    if (ecx) *ecx = 0;
    if (edx) *edx = 0;
#endif
}

/**
 * Get CPU ID (APIC ID)
 * Returns the current CPU's APIC ID using CPUID
 */
uint32_t arch_get_cpu_id(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 (basic info) */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 - contains APIC ID in bits 31:24 of EBX */
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        return (ebx >> 24) & 0xFF;
    }
#endif
    /* Default to 0 if CPUID not available or not x86 */
    return 0;
}

/**
 * Get number of CPUs
 * For now, detects if HT/SMT is enabled and logical cores
 * Full SMP detection would require ACPI MADT parsing
 */
uint32_t arch_get_cpu_count(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 */
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        
        /* Check if HT/SMT is supported */
        if (edx & (1 << 28))  /* HTT bit */
        {
            /* Get number of logical processors per package */
            /* Bits 23:16 of EBX contain maximum number of addressable IDs */
            uint32_t max_logical_cores = (ebx >> 16) & 0xFF;
            if (max_logical_cores > 0)
                return max_logical_cores;
        }
    }
    
    /* Check if CPUID supports leaf 0xB (extended topology) */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0xB)
    {
        /* Try to get logical processor count from leaf 0xB */
        cpuid(0xB, 0, &eax, &ebx, &ecx, &edx);
        if (ebx != 0)
        {
            /* EBX contains number of logical processors at this level */
            return ebx & 0xFFFF;
        }
    }
#endif
    /* Default to 1 for single-core or non-x86 */
    return 1;
}

/**
 * Get CPU vendor string using CPUID
 * @vendor_buf: Buffer to store vendor string (must be at least 13 bytes)
 * Returns: 0 on success, -1 on failure
 */
int arch_get_cpu_vendor(char *vendor_buf)
{
#if defined(__x86_64__) || defined(__i386__)
    if (!vendor_buf)
        return -1;
    
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    
    /* Vendor string is in EBX, EDX, ECX (in that order) */
    memcpy(vendor_buf, &ebx, 4);
    memcpy(vendor_buf + 4, &edx, 4);
    memcpy(vendor_buf + 8, &ecx, 4);
    vendor_buf[12] = '\0';
    
    return 0;
#else
    /* Non-x86: return generic vendor */
    strncpy(vendor_buf, "Unknown", 8);
    return -1;
#endif
}

/**
 * Get CPU family, model, and stepping from CPUID
 * @family: Output for CPU family
 * @model: Output for CPU model
 * @stepping: Output for CPU stepping
 * Returns: 0 on success, -1 on failure
 */
int arch_get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 - contains family/model/stepping in EAX */
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        
        if (family)
        {
            /* Extract family: bits 11:8, extended family: bits 27:20 */
            uint32_t base_family = (eax >> 8) & 0xF;
            uint32_t ext_family = (eax >> 20) & 0xFF;
            *family = (ext_family > 0) ? (base_family + ext_family) : base_family;
        }
        
        if (model)
        {
            /* Extract model: bits 7:4, extended model: bits 19:16 */
            uint32_t base_model = (eax >> 4) & 0xF;
            uint32_t ext_model = (eax >> 16) & 0xF;
            *model = (ext_model > 0) ? ((ext_model << 4) + base_model) : base_model;
        }
        
        if (stepping)
        {
            /* Extract stepping: bits 3:0 */
            *stepping = eax & 0xF;
        }
        
        return 0;
    }
#endif
    /* Fallback values */
    if (family) *family = 0;
    if (model) *model = 0;
    if (stepping) *stepping = 0;
    return -1;
}

/**
 * Get CPUID maximum leaf (EAX from CPUID.0)
 * @max_leaf: Output for maximum supported leaf
 * Returns: 0 on success, -1 on failure
 */
int arch_get_cpuid_max_leaf(uint32_t *max_leaf)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (max_leaf)
        *max_leaf = eax;
    return 0;
#else
    if (max_leaf)
        *max_leaf = 0;
    return -1;
#endif
}

/**
 * Get CPU brand string from CPUID (leaves 0x80000002-0x80000004)
 * @buf: Output buffer (at least 49 bytes recommended)
 * @size: Buffer size
 * Returns: 0 on success, -1 if not supported or failure
 */
int arch_get_cpu_brand_string(char *buf, size_t size)
{
#if defined(__x86_64__) || defined(__i386__)
    if (!buf || size < 49)
        return -1;
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x80000000U, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004U)
        return -1;
    uint32_t *u = (uint32_t *)buf;
    cpuid(0x80000002U, 0, &u[0], &u[1], &u[2], &u[3]);
    cpuid(0x80000003U, 0, &u[4], &u[5], &u[6], &u[7]);
    cpuid(0x80000004U, 0, &u[8], &u[9], &u[10], &u[11]);
    buf[48] = '\0';
    /* Trim trailing spaces */
    for (int i = 47; i >= 0 && (buf[i] == ' ' || buf[i] == '\0'); i--)
        buf[i] = '\0';
    return 0;
#else
    (void)buf;
    (void)size;
    return -1;
#endif
}

/**
 * Get CPU feature bits from CPUID.1 (EDX and ECX)
 * @out_edx: Output for EDX feature flags (NULL allowed)
 * @out_ecx: Output for ECX feature flags (NULL allowed)
 * Returns: 0 on success, -1 on failure
 */
int arch_get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 1)
        return -1;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (out_edx)
        *out_edx = edx;
    if (out_ecx)
        *out_ecx = ecx;
    return 0;
#else
    if (out_edx) *out_edx = 0;
    if (out_ecx) *out_ecx = 0;
    return -1;
#endif
}

/**
 * Get CLFLUSH line size from CPUID.1 EBX bits 15:8 (units: 8 bytes)
 * Returns: size in bytes, or 0 if not reported
 */
uint32_t arch_get_cpu_clflush_size(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 1)
        return 0;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return ((ebx >> 8) & 0xFF) * 8;
#else
    return 0;
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

void arch_syscall_init(void)
{
    /* x86 syscall entry is initialized via IDT setup; ARM64 wiring is incremental. */
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