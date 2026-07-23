/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu_early.c
 * Description: Early ARM64 identity map (TTBR0) + Device/user page map for QEMU virt.
 *
 * Reference: Linux arm64 head.S idmap/TTBR0 path (simplified — no TTBR1 high map).
 * QEMU virt: DRAM @ 0x40000000, PL011 @ 0x09000000, GIC @ 0x08000000.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mmu_early.h"

#include <arch/common/arch_portable.h>
#include <stdint.h>

#define EINVAL 22
#define ENODEV 19

#define PAGE_SIZE        4096UL
#define BLOCK_2M         0x200000UL
#define PTE_ENTRIES      512UL

#define PTE_VALID        (1UL << 0)
#define PTE_TABLE        (1UL << 1)
#define PTE_TYPE_BLOCK   (PTE_VALID)
#define PTE_TYPE_TABLE   (PTE_VALID | PTE_TABLE)
#define PTE_TYPE_PAGE    (PTE_VALID | PTE_TABLE)

#define PTE_ATTR_SHIFT   2
#define PTE_AF           (1UL << 10)
#define PTE_SH_INNER     (3UL << 8)
#define PTE_AP_RW_EL1    (0UL << 6)
#define PTE_AP_RW_EL0    (1UL << 6)
#define PTE_UXN          (1UL << 54)
#define PTE_PXN          (1UL << 53)

#define ATTR_DEVICE      0
#define ATTR_NORMAL      1

#define VIRT_UART_BASE   0x09000000UL
#define VIRT_DRAM_BASE   0x40000000UL
#define VIRT_DRAM_END    (VIRT_DRAM_BASE + 0x40000000UL)

#define L1_INDEX(va)     (((va) >> 30) & 0x1FFUL)
#define L2_INDEX(va)     (((va) >> 21) & 0x1FFUL)
#define L3_INDEX(va)     (((va) >> 12) & 0x1FFUL)

#define MAIR_ATTR_DEVICE 0x00UL
#define MAIR_ATTR_NORMAL 0xffUL

static uint64_t l1_table[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l1_table_b[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_mmio[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_dram[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
#define L3_POOL_MAX 32
static uint64_t l3_pool[L3_POOL_MAX][PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static int l3_pool_l2[L3_POOL_MAX]; /* L2 index owning this L3, or -1 */
static int g_mmu_on;
static int g_dram_split;
#define USER_PAGE_MAX 2048
static uint64_t g_user_pages[USER_PAGE_MAX];
static unsigned g_user_page_count;

static uint64_t pte_block_2m_device(uint64_t pa)
{
	return PTE_TYPE_BLOCK
	     | ((uint64_t)ATTR_DEVICE << PTE_ATTR_SHIFT)
	     | PTE_AP_RW_EL1
	     | PTE_SH_INNER
	     | PTE_AF
	     | PTE_UXN
	     | (pa & 0x0000FFFFFFE00000UL);
}

static uint64_t pte_block_2m_dram_el1(uint64_t pa)
{
	/* UXN clear: EL0 may execute kernel text in DRAM (el0_entry). */
	return PTE_TYPE_BLOCK
	     | ((uint64_t)ATTR_NORMAL << PTE_ATTR_SHIFT)
	     | PTE_AP_RW_EL1
	     | PTE_SH_INNER
	     | PTE_AF
	     | (pa & 0x0000FFFFFFE00000UL);
}

static uint64_t pte_page_4k(uint64_t pa, uint64_t ap, int uxn, int pxn)
{
	uint64_t pte = PTE_TYPE_PAGE
		     | ((uint64_t)ATTR_NORMAL << PTE_ATTR_SHIFT)
		     | ap
		     | PTE_SH_INNER
		     | PTE_AF
		     | (pa & 0x0000FFFFFFFFF000UL);

	if (uxn)
	{
		pte |= PTE_UXN;
	}
	if (pxn)
	{
		pte |= PTE_PXN;
	}
	return pte;
}

static uint64_t pte_table(uint64_t table_pa)
{
	return PTE_TYPE_TABLE | (table_pa & 0x0000FFFFFFFFF000UL);
}

static void zero_table(uint64_t *tbl)
{
	unsigned i;

	for (i = 0; i < PTE_ENTRIES; i++)
	{
		tbl[i] = 0;
	}
}

static void tlb_invalidate(void)
{
	__asm__ volatile("tlbi vmalle1" ::: "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

static void build_idmap(void)
{
	uint64_t l2_mmio_pa = (uint64_t)(uintptr_t)l2_mmio;
	uint64_t l2_dram_pa = (uint64_t)(uintptr_t)l2_dram;
	uint64_t uart_block = VIRT_UART_BASE & ~0x1FFFFFUL;
	unsigned i;

	zero_table(l1_table);
	zero_table(l2_mmio);
	zero_table(l2_dram);
	{
		unsigned p;

		for (p = 0; p < L3_POOL_MAX; p++)
		{
			zero_table(l3_pool[p]);
			l3_pool_l2[p] = -1;
		}
	}
	g_dram_split = 1;
	g_user_page_count = 0;

	l1_table[L1_INDEX(VIRT_UART_BASE)] = pte_table(l2_mmio_pa);
	l2_mmio[L2_INDEX(VIRT_UART_BASE)] = pte_block_2m_device(uart_block);

	/*
	 * DRAM as 512×2 MiB EL1 (UXN clear). Never install EL0 on a 1 GiB block
	 * (hangs QEMU). arm64_mmu_map_user_page installs one 4K EL0 page via L3.
	 */
	for (i = 0; i < PTE_ENTRIES; i++)
	{
		l2_dram[i] = pte_block_2m_dram_el1(VIRT_DRAM_BASE + (uint64_t)i * BLOCK_2M);
	}
	l1_table[L1_INDEX(VIRT_DRAM_BASE)] = pte_table(l2_dram_pa);
}

static void mmu_configure_and_enable(uint64_t ttbr0)
{
	uint64_t mair;
	uint64_t tcr;
	uint64_t sctlr;

	mair = (MAIR_ATTR_DEVICE << 0) | (MAIR_ATTR_NORMAL << 8);
	__asm__ volatile("msr mair_el1, %0" :: "r"(mair) : "memory");

	tcr = (25UL << 0)
	    | (0UL << 14)
	    | (1UL << 8)
	    | (1UL << 10)
	    | (3UL << 12)
	    | (2UL << 32);
	__asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");

	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0) : "memory");
	__asm__ volatile("isb" ::: "memory");

	tlb_invalidate();

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr |= (1UL << 0);
	sctlr |= (1UL << 2);
	sctlr |= (1UL << 12);
	sctlr &= ~(1UL << 25);
	sctlr &= ~(1UL << 19);
	__asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

int arm64_mmu_early_enable(void)
{
	uint64_t el;
	uint64_t ttbr0;

	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	el = (el >> 2) & 3UL;
	if (el != 1UL)
	{
		return -EINVAL;
	}

	build_idmap();
	ttbr0 = (uint64_t)(uintptr_t)l1_table;
	mmu_configure_and_enable(ttbr0);
	g_mmu_on = 1;
	return 0;
}

int arm64_mmu_early_verify(void)
{
	uint64_t ttbr0;
	uint64_t sctlr;
	uint64_t expect = (uint64_t)(uintptr_t)l1_table;

	if (!g_mmu_on)
	{
		return -ENODEV;
	}

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

	if ((ttbr0 & ~0x1UL) != (expect & ~0x1UL))
	{
		return -EINVAL;
	}
	if ((sctlr & 1UL) == 0)
	{
		return -EINVAL;
	}
	return 0;
}

int arm64_mmu_ttbr_dual_smoke(void)
{
	unsigned i;
	uint64_t ttbr;
	uint64_t root_a = (uint64_t)(uintptr_t)l1_table;
	uint64_t root_b = (uint64_t)(uintptr_t)l1_table_b;
	volatile uint32_t *probe;

	if (!g_mmu_on)
		return -ENODEV;

	arm64_mmu_clone_root_b();

	mm_activate((uintptr_t)root_b);
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if ((ttbr & ~0x1UL) != (root_b & ~0x1UL))
		return -EINVAL;

	/* Touch DRAM under the new TTBR0 (identity map still valid). */
	probe = (volatile uint32_t *)(uintptr_t)VIRT_DRAM_BASE;
	(void)*probe;

	mm_activate((uintptr_t)root_a);
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if ((ttbr & ~0x1UL) != (root_a & ~0x1UL))
		return -EINVAL;

	return 0;
}

void arm64_mmu_clone_root_b(void)
{
	unsigned i;

	for (i = 0; i < PTE_ENTRIES; i++)
		l1_table_b[i] = l1_table[i];
}

uint64_t arm64_mmu_root_a(void)
{
	return (uint64_t)(uintptr_t)l1_table;
}

uint64_t arm64_mmu_root_b(void)
{
	return (uint64_t)(uintptr_t)l1_table_b;
}

int arm64_mmu_map_device_block(uint64_t pa)
{
	uint64_t block = pa & ~0x1FFFFFUL;
	uint64_t l1_idx = L1_INDEX(block);
	uint64_t l2_idx = L2_INDEX(block);
	uint64_t l2_pa = (uint64_t)(uintptr_t)l2_mmio;

	if (!g_mmu_on)
	{
		return -ENODEV;
	}

	if (l1_idx != L1_INDEX(VIRT_UART_BASE))
	{
		return -EINVAL;
	}
	if ((l1_table[l1_idx] & PTE_TYPE_TABLE) != PTE_TYPE_TABLE)
	{
		l1_table[l1_idx] = pte_table(l2_pa);
	}

	l2_mmio[l2_idx] = pte_block_2m_device(block);
	tlb_invalidate();
	return 0;
}

static int dram_ensure_l2(void)
{
	return g_dram_split ? 0 : -EINVAL;
}

static uint64_t *l3_table_for_l2(uint64_t l2_idx)
{
	unsigned i;
	int free_slot = -1;

	for (i = 0; i < L3_POOL_MAX; i++)
	{
		if (l3_pool_l2[i] == (int)l2_idx)
			return l3_pool[i];
		if (free_slot < 0 && l3_pool_l2[i] < 0)
			free_slot = (int)i;
	}
	if (free_slot < 0)
		return NULL;
	l3_pool_l2[free_slot] = (int)l2_idx;
	zero_table(l3_pool[free_slot]);
	return l3_pool[free_slot];
}

static int user_page_note(uint64_t page)
{
	unsigned i;

	for (i = 0; i < g_user_page_count; i++)
	{
		if (g_user_pages[i] == page)
			return 0;
	}
	if (g_user_page_count >= USER_PAGE_MAX)
		return -EINVAL;
	g_user_pages[g_user_page_count++] = page;
	return 0;
}

int arm64_mmu_map_user_page_flags(uint64_t pa, int exec_el0)
{
	uint64_t page = pa & ~(PAGE_SIZE - 1UL);
	uint64_t l2_idx;
	uint64_t l3_idx;
	uint64_t *l3;
	uint64_t l3_pa;
	int uxn = exec_el0 ? 0 : 1;

	if (!g_mmu_on)
		return -ENODEV;
	if (page < VIRT_DRAM_BASE || page >= VIRT_DRAM_END)
		return -EINVAL;
	if (dram_ensure_l2() != 0)
		return -EINVAL;

	l2_idx = L2_INDEX(page);
	l3_idx = L3_INDEX(page);
	l3 = l3_table_for_l2(l2_idx);
	if (!l3)
		return -EINVAL;

	l3_pa = (uint64_t)(uintptr_t)l3;
	/* First split of this 2 MiB block: install L3 table. */
	if ((l2_dram[l2_idx] & PTE_TYPE_TABLE) != PTE_TYPE_TABLE ||
	    (l2_dram[l2_idx] & 0x0000FFFFFFFFF000UL) != (l3_pa & 0x0000FFFFFFFFF000UL))
	{
		l2_dram[l2_idx] = pte_table(l3_pa);
	}

	l3[l3_idx] = pte_page_4k(page, PTE_AP_RW_EL0, uxn, 1);
	if (user_page_note(page) != 0)
		return -EINVAL;
	tlb_invalidate();
	return 0;
}

int arm64_mmu_map_user_page(uint64_t pa)
{
	return arm64_mmu_map_user_page_flags(pa, 0);
}

int arm64_mmu_user_buf_ok(uint64_t va, uint64_t len)
{
	uint64_t end;
	uint64_t page;
	unsigned i;
	int found;

	if (g_user_page_count == 0 || len == 0)
		return 0;
	if (va + len < va)
		return 0;
	end = va + len;
	for (page = va & ~(PAGE_SIZE - 1UL); page < end; page += PAGE_SIZE)
	{
		found = 0;
		for (i = 0; i < g_user_page_count; i++)
		{
			if (g_user_pages[i] == page)
			{
				found = 1;
				break;
			}
		}
		if (!found)
			return 0;
	}
	return 1;
}
