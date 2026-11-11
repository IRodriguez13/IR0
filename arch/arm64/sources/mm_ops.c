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
#define A64_PTE_AP_RO     (1ULL << 7)
#define A64_PTE_NG        (1ULL << 11)
#define A64_PTE_PXN       (1ULL << 53)
#define A64_PTE_UXN       (1ULL << 54)
#define A64_PTE_SH_INNER  (3ULL << 8)
#define A64_PTE_ATTR(i)   ((uint64_t)(i) << 2)
#define A64_PTE_ATTR_MASK (7ULL << 2)
/* MAIR_EL1 slots programmed by mmu.c. */
#define A64_MAIR_DEVICE   0U
#define A64_MAIR_NORMAL   1U
#define A64_MAIR_NORMAL_NC 2U
#define A64_PTE_SW_COW    (1ULL << 55)
/* Software bit 56: PROT_NONE user page, mapped EL1-only, never executable. */
#define A64_PTE_SW_PROTNONE (1ULL << 56)
#define A64_PTE_PFN_MASK  0x0000FFFFFFFFF000ULL
#define A64_INDEX_MASK    0x1FFUL
#define A64_SCTLR_M       (1ULL << 0)

void mm_activate(uintptr_t root)
{
	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(root) : "memory");
	__asm__ volatile("isb" ::: "memory");
	tlb_invalidate_all();
}

uintptr_t mm_current_root(void)
{
	uintptr_t root;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root));
	return root;
}

void tlb_invalidate_page(uintptr_t va)
{
	/* VA → page; TLBI VAE1 (EL1 stage-1). */
	uintptr_t page = va >> 12;

	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("tlbi vaae1, %0" :: "r"(page) : "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

void tlb_invalidate_all(void)
{
	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("tlbi vmalle1" ::: "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

unsigned long irq_save(void)
{
	unsigned long daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	__asm__ volatile("msr daifset, #2" ::: "memory");
	return daif;
}

void irq_restore(unsigned long flags)
{
	__asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

static uint64_t mm_read_ctrl0(void)
{
	uint64_t sctlr;

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	return sctlr;
}

static void mm_write_ctrl0(uint64_t value)
{
	__asm__ volatile("msr sctlr_el1, %0" :: "r"(value) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void mm_enable_translation(void)
{
	uint64_t sctlr = mm_read_ctrl0();

	sctlr |= A64_SCTLR_M;
	mm_write_ctrl0(sctlr);
}

int mm_translation_enabled(void)
{
	return (mm_read_ctrl0() & A64_SCTLR_M) != 0;
}

int mm_translation_ready(void)
{
	/* Early platform setup owns MAIR/TCR/TTBR initialization. */
	return 1;
}

void mm_va_indices(uintptr_t va, size_t idx[4])
{
	/* Same 4-level 9-bit split as x86-64 4 KiB (VA bits 47:12). */
	idx[0] = ((uint64_t)va >> 39) & A64_INDEX_MASK;
	idx[1] = ((uint64_t)va >> 30) & A64_INDEX_MASK;
	idx[2] = ((uint64_t)va >> 21) & A64_INDEX_MASK;
	idx[3] = ((uint64_t)va >> 12) & A64_INDEX_MASK;
}

size_t mm_table_entries(void)
{
	return 512;
}

uint64_t mm_large_leaf_pages(unsigned int level)
{
	if (level == 1U)
		return 512ULL * 512ULL;
	return 512;
}

uintptr_t mm_va_from_indices(const size_t idx[4])
{
	if (!idx)
		return 0;
	return ((uintptr_t)idx[0] << 39) | ((uintptr_t)idx[1] << 30) |
	       ((uintptr_t)idx[2] << 21) | ((uintptr_t)idx[3] << 12);
}

int mm_pte_present(uint64_t e)
{
	return (e & A64_PTE_VALID) != 0;
}

int mm_pte_is_swap(uint64_t e)
{
	return !mm_pte_present(e) && e != 0 && ((e & 3ULL) == 0);
}

void mm_pte_swap_decode(uint64_t e, unsigned *type, uint64_t *offset)
{
	if (type)
		*type = (unsigned)((e >> 2) & 0x3fULL);
	if (offset)
		*offset = e >> 8;
}

uint64_t mm_make_swap_pte(unsigned type, uint64_t offset)
{
	return (offset << 8) | (((uint64_t)type & 0x3fULL) << 2);
}

int mm_pte_large(uint64_t e)
{
	/* Block descriptor: valid + not table (bit1 clear) at L1/L2. */
	return mm_pte_present(e) && ((e & A64_PTE_TABLE) == 0);
}

int mm_pte_executable(uint64_t e)
{
	return (e & A64_PTE_UXN) == 0;
}

uintptr_t mm_pte_phys(uint64_t e)
{
	return (uintptr_t)(e & A64_PTE_PFN_MASK);
}

uint64_t mm_pte_mapping_flags(uint64_t e)
{
	uint64_t flags = 0;

	if (mm_pte_present(e))
		flags |= IR0_MM_MAP_PRESENT;
	if (!(e & A64_PTE_AP_RO))
		flags |= IR0_MM_MAP_WRITE;
	if (e & (A64_PTE_AP_EL0 | A64_PTE_SW_PROTNONE))
		flags |= IR0_MM_MAP_USER;
	if (e & A64_PTE_SW_PROTNONE)
		flags |= IR0_MM_MAP_PROTNONE;
	if (!(e & A64_PTE_NG))
		flags |= IR0_MM_MAP_GLOBAL;
	if (e & A64_PTE_SW_COW)
		flags |= IR0_MM_MAP_COW;
	if (mm_pte_executable(e))
		flags |= IR0_MM_MAP_EXEC;
	if ((e & A64_PTE_ATTR_MASK) == A64_PTE_ATTR(A64_MAIR_DEVICE))
		flags |= IR0_MM_MAP_NOCACHE;
	else if ((e & A64_PTE_ATTR_MASK) == A64_PTE_ATTR(A64_MAIR_NORMAL_NC))
		flags |= IR0_MM_MAP_WRITECOMBINE;
	if (e & A64_PTE_AF)
		flags |= IR0_MM_MAP_ACCESSED;
	/*
	 * No hardware dirty tracking (no DBM): report writable or COW pages as
	 * dirty so reclaim never drops data it cannot prove clean.
	 */
	if (flags & (IR0_MM_MAP_WRITE | IR0_MM_MAP_COW))
		flags |= IR0_MM_MAP_DIRTY;
	if (mm_pte_large(e))
		flags |= IR0_MM_MAP_LARGE;
	return flags;
}

void mm_pte_mark_cow(uint64_t *e)
{
	if (e)
		*e |= A64_PTE_AP_RO | A64_PTE_SW_COW | A64_PTE_NG;
}

uint64_t mm_make_table_pte(uintptr_t phys, int user)
{
	uint64_t e = ((uint64_t)phys & A64_PTE_PFN_MASK) | A64_PTE_VALID | A64_PTE_TABLE;

	(void)user; /* AP on table descriptors deferred until full aarch64 walker */
	return e;
}

/*
 * Stage-1 leaf descriptor (ARM DDI 0487, D8.3). An L3 page needs bits[1:0] =
 * 0b11; an L1/L2 block uses 0b01. Normal memory takes the inner-shareable
 * write-back MAIR slot, IR0_MM_MAP_NOCACHE the Device-nGnRnE slot and
 * IR0_MM_MAP_WRITECOMBINE the Normal non-cacheable slot. Like Linux,
 * EL0 mappings are never executable at EL1 (PXN) and kernel mappings are
 * never executable at EL0 (UXN).
 */
uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec)
{
	uint64_t e = ((uint64_t)phys & A64_PTE_PFN_MASK) | A64_PTE_VALID | A64_PTE_AF;

	if (!(flags12 & IR0_MM_MAP_LARGE))
		e |= A64_PTE_TABLE;
	if (flags12 & IR0_MM_MAP_NOCACHE)
		e |= A64_PTE_ATTR(A64_MAIR_DEVICE);
	else if (flags12 & IR0_MM_MAP_WRITECOMBINE)
		e |= A64_PTE_ATTR(A64_MAIR_NORMAL_NC) | A64_PTE_SH_INNER;
	else
		e |= A64_PTE_ATTR(A64_MAIR_NORMAL) | A64_PTE_SH_INNER;
	if (flags12 & IR0_MM_MAP_PROTNONE)
	{
		e |= A64_PTE_SW_PROTNONE | A64_PTE_NG | A64_PTE_AP_RO |
		     A64_PTE_UXN | A64_PTE_PXN;
		return e;
	}
	if (flags12 & IR0_MM_MAP_USER)
	{
		e |= A64_PTE_AP_EL0 | A64_PTE_NG | A64_PTE_PXN;
		if (!exec)
			e |= A64_PTE_UXN;
	}
	else
	{
		e |= A64_PTE_UXN;
		if (!exec)
			e |= A64_PTE_PXN;
	}
	if (!(flags12 & IR0_MM_MAP_WRITE))
		e |= A64_PTE_AP_RO;
	if (flags12 & IR0_MM_MAP_COW)
		e |= A64_PTE_SW_COW | A64_PTE_AP_RO;
	return e;
}

void mm_pte_set_user(uint64_t *e)
{
	(void)e;
	/* ARM table descriptors default to no hierarchical AP restriction. */
}
