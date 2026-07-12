/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_ops.c
 * Description: ARM64 MM activate / TLB / IRQ save behind arch_portable (F8-facade-mm).
 *
 * Freestanding process mm is not claimed — TTBR0 ops for facade parity / future AS.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>

#include <stddef.h>
#include <stdint.h>

/* aarch64 4K granule descriptor bits (Linux pte present / block / phys). */
#define A64_PTE_VALID     0x1ULL
#define A64_PTE_TABLE     0x2ULL
#define A64_PTE_AF        (1ULL << 10)
#define A64_PTE_AP_EL0    (1ULL << 6) /* AP[1]=1 → EL0 access when AP[2]=0 */
#define A64_PTE_UXN       (1ULL << 54)
#define A64_PTE_PFN_MASK  0x0000FFFFFFFFF000ULL
#define A64_INDEX_MASK    0x1FFUL

void arch_mm_activate(uintptr_t root)
{
	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(root) : "memory");
	__asm__ volatile("isb" ::: "memory");
	arch_tlb_invalidate_all();
}

uintptr_t arch_mm_current_root(void)
{
	uintptr_t root;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root));
	return root;
}

void arch_tlb_invalidate_page(uintptr_t va)
{
	/* VA → page; TLBI VAE1 (EL1 stage-1). */
	uintptr_t page = va >> 12;

	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("tlbi vaae1, %0" :: "r"(page) : "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

void arch_tlb_invalidate_all(void)
{
	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("tlbi vmalle1" ::: "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

unsigned long arch_irq_save(void)
{
	unsigned long daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	__asm__ volatile("msr daifset, #2" ::: "memory");
	return daif;
}

void arch_irq_restore(unsigned long flags)
{
	__asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

uint64_t arch_mm_read_ctrl0(void)
{
	uint64_t sctlr;

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	return sctlr;
}

void arch_mm_write_ctrl0(uint64_t value)
{
	__asm__ volatile("msr sctlr_el1, %0" :: "r"(value) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

uint64_t arch_mm_read_ctrl1(void)
{
	/* No CR4 analogue claimed for freestanding ARM yet. */
	return 0;
}

void arch_mm_va_indices(uintptr_t va, size_t idx[4])
{
	/* Same 4-level 9-bit split as x86-64 4 KiB (VA bits 47:12). */
	idx[0] = ((uint64_t)va >> 39) & A64_INDEX_MASK;
	idx[1] = ((uint64_t)va >> 30) & A64_INDEX_MASK;
	idx[2] = ((uint64_t)va >> 21) & A64_INDEX_MASK;
	idx[3] = ((uint64_t)va >> 12) & A64_INDEX_MASK;
}

int arch_mm_pte_present(uint64_t e)
{
	return (e & A64_PTE_VALID) != 0;
}

int arch_mm_pte_large(uint64_t e)
{
	/* Block descriptor: valid + not table (bit1 clear) at L1/L2. */
	return arch_mm_pte_present(e) && ((e & A64_PTE_TABLE) == 0);
}

uintptr_t arch_mm_pte_phys(uint64_t e)
{
	return (uintptr_t)(e & A64_PTE_PFN_MASK);
}

uint64_t arch_mm_make_table_pte(uintptr_t phys, int user)
{
	uint64_t e = ((uint64_t)phys & A64_PTE_PFN_MASK) | A64_PTE_VALID | A64_PTE_TABLE;

	(void)user; /* AP on table descriptors deferred until full aarch64 walker */
	return e;
}

uint64_t arch_mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec)
{
	uint64_t e = ((uint64_t)phys & A64_PTE_PFN_MASK) | A64_PTE_VALID | A64_PTE_AF;

	(void)flags12;
	if (!exec)
		e |= A64_PTE_UXN;
	return e;
}

void arch_mm_pte_set_user(uint64_t *e)
{
	if (e)
		*e |= A64_PTE_AP_EL0;
}
