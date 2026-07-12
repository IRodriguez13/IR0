/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu_early.c
 * Description: Early ARM64 identity map (TTBR0) for QEMU virt DRAM + PL011 UART.
 *
 * Reference: Linux arm64 head.S idmap/TTBR0 path (simplified — no TTBR1 high map).
 * QEMU virt: DRAM @ 0x40000000, PL011 UART0 @ 0x09000000.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mmu_early.h"

#include <stdint.h>

/* Minimal errno for freestanding boot image (no libc / ir0 errno pull-in). */
#define EINVAL 22

#define PAGE_SIZE        4096UL
#define PTE_ENTRIES      512UL

/* Descriptor bits (4K granule). */
#define PTE_VALID        (1UL << 0)
#define PTE_TABLE        (1UL << 1)
#define PTE_TYPE_BLOCK   (PTE_VALID)                 /* 0b01 */
#define PTE_TYPE_TABLE   (PTE_VALID | PTE_TABLE)     /* 0b11 */

#define PTE_ATTR_SHIFT   2
#define PTE_AF           (1UL << 10)
#define PTE_SH_INNER     (3UL << 8)
#define PTE_AP_RW_EL1    (0UL << 6) /* EL1 RW, EL0 none */
#define PTE_AP_RW_ANY    (1UL << 6) /* EL1+EL0 RW (AP=01) */
#define PTE_UXN          (1UL << 54)

#define ATTR_DEVICE      0 /* MAIR AttrIdx0 — Device-nGnRnE */
#define ATTR_NORMAL      1 /* MAIR AttrIdx1 — Normal WB */

/* QEMU virt map. */
#define VIRT_UART_BASE   0x09000000UL
#define VIRT_DRAM_BASE   0x40000000UL

/*
 * 39-bit VA (T0SZ=25): TTBR0 points at L1. Indices:
 *   L1 [38:30], L2 [29:21], L3 [20:12].
 */
#define L1_INDEX(va)     (((va) >> 30) & 0x1FFUL)
#define L2_INDEX(va)     (((va) >> 21) & 0x1FFUL)

#define MAIR_ATTR_DEVICE 0x00UL
#define MAIR_ATTR_NORMAL 0xffUL /* Outer/Inner Write-Back Non-transient */

static uint64_t l1_table[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_mmio[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

static uint64_t pte_block_1g(uint64_t pa, unsigned attr_idx, uint64_t ap, int uxn)
{
	uint64_t pte = PTE_TYPE_BLOCK
		     | ((uint64_t)attr_idx << PTE_ATTR_SHIFT)
		     | ap
		     | PTE_SH_INNER
		     | PTE_AF
		     | (pa & 0x0000FFFFC0000000UL);

	if (uxn)
	{
		pte |= PTE_UXN;
	}
	return pte;
}

static uint64_t pte_block_2m(uint64_t pa, unsigned attr_idx)
{
	return PTE_TYPE_BLOCK
	     | ((uint64_t)attr_idx << PTE_ATTR_SHIFT)
	     | PTE_AP_RW_EL1
	     | PTE_SH_INNER
	     | PTE_AF
	     | PTE_UXN
	     | (pa & 0x0000FFFFFFE00000UL);
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

static void build_idmap(void)
{
	uint64_t l2_pa = (uint64_t)(uintptr_t)l2_mmio;
	uint64_t uart_block = VIRT_UART_BASE & ~0x1FFFFFUL; /* 2 MiB align */

	zero_table(l1_table);
	zero_table(l2_mmio);

	/* Low 1 GiB: L2 table so UART MMIO can be Device-mapped. */
	l1_table[L1_INDEX(VIRT_UART_BASE)] = pte_table(l2_pa);
	l2_mmio[L2_INDEX(VIRT_UART_BASE)] = pte_block_2m(uart_block, ATTR_DEVICE);

	/*
	 * DRAM 1 GiB identity @ 0x40000000.
	 * AP=EL1-only data; UXN clear so EL0 may execute (F7.3 SVC smoke).
	 * Note: AP=EL0-RW (AP=01) on this 1GiB block hangs QEMU virt early enable —
	 * keep EL1-only AP until a finer L2 map is proven.
	 */
	l1_table[L1_INDEX(VIRT_DRAM_BASE)] =
		pte_block_1g(VIRT_DRAM_BASE, ATTR_NORMAL, PTE_AP_RW_EL1, 0);
}

static void mmu_configure_and_enable(uint64_t ttbr0)
{
	uint64_t mair;
	uint64_t tcr;
	uint64_t sctlr;

	mair = (MAIR_ATTR_DEVICE << 0) | (MAIR_ATTR_NORMAL << 8);
	__asm__ volatile("msr mair_el1, %0" :: "r"(mair) : "memory");

	/*
	 * T0SZ=25 → 39-bit VA; TG0=4K (0); IRGN/ORGN WB WA (1); SH Inner (3);
	 * IPS=40-bit (2) enough for virt DRAM.
	 */
	tcr = (25UL << 0)   /* T0SZ */
	    | (0UL << 14)   /* TG0 4K */
	    | (1UL << 8)    /* IRGN0 WB WA */
	    | (1UL << 10)   /* ORGN0 WB WA */
	    | (3UL << 12)   /* SH0 Inner Shareable */
	    | (2UL << 32);  /* IPS 40 bits */
	__asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");

	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0) : "memory");
	__asm__ volatile("isb" ::: "memory");

	/* Invalidate TLB before enabling translations. */
	__asm__ volatile("tlbi vmalle1" ::: "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr |= (1UL << 0);  /* M — MMU */
	sctlr |= (1UL << 2);  /* C — data cache */
	sctlr |= (1UL << 12); /* I — instruction cache */
	/* Clear EE (little-endian) and WXN if set by firmware. */
	sctlr &= ~(1UL << 25); /* EE */
	sctlr &= ~(1UL << 19); /* WXN */
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
		/* QEMU -kernel virt should enter at EL1; refuse other ELs for F7.1. */
		return -EINVAL;
	}

	build_idmap();
	ttbr0 = (uint64_t)(uintptr_t)l1_table;
	mmu_configure_and_enable(ttbr0);
	return 0;
}
