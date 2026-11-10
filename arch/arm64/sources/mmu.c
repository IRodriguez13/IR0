/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu.c
 * Description: ARM64 boot MMU. Builds the 4-level 48-bit TTBR0 identity map
 *              used before the first process and supplies the supervisor
 *              identity (DRAM + Device blocks) of every user root. User
 *              mappings are owned by the common page-table walker (mm/paging.c)
 *              through the mm_ops descriptor encoders.
 *
 * Reference: Linux arch/arm64/kernel/head.S (idmap), arch/arm64/mm/proc.S
 * (TCR/MAIR). IR0 keeps the kernel identity-mapped in TTBR0 on both ISAs until
 * the higher-half split lands; TTBR1 walks are disabled (EPD1).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mmu.h"

#include <arch/common/arch_portable.h>
#include <arm64_board.h>
#include <ir0/errno.h>
#include <ir0/paging.h>
#include <stdint.h>

#define PAGE_SIZE        4096UL
#define BLOCK_2M         0x200000UL
#define PTE_ENTRIES      512UL

#define PTE_VALID        (1UL << 0)
#define PTE_TABLE        (1UL << 1)
#define PTE_TYPE_BLOCK   (PTE_VALID)
#define PTE_TYPE_TABLE   (PTE_VALID | PTE_TABLE)
#define PTE_ATTR_SHIFT   2
#define PTE_AF           (1UL << 10)
#define PTE_SH_INNER     (3UL << 8)
#define PTE_PXN          (1UL << 53)
#define PTE_UXN          (1UL << 54)
#define PTE_ADDR_MASK    0x0000FFFFFFFFF000UL
#define PTE_BLOCK_MASK   0x0000FFFFFFE00000UL

/* MAIR_EL1 slots; arch/arm64/sources/mm_ops.c encodes the same indices. */
#define ATTR_DEVICE      0
#define ATTR_NORMAL      1
#define ATTR_NORMAL_NC   2
#define MAIR_ATTR_DEVICE 0x00UL /* Device-nGnRnE */
#define MAIR_ATTR_NORMAL 0xffUL /* Normal, inner/outer write-back */
#define MAIR_ATTR_NORMAL_NC 0x44UL /* Normal, inner/outer non-cacheable */

/* TCR_EL1: T0SZ=16 (48-bit VA, walk starts at L0), 4 KiB granule. */
#define TCR_T0SZ_48      16UL
#define TCR_IRGN0_WBWA   (1UL << 8)
#define TCR_ORGN0_WBWA   (1UL << 10)
#define TCR_SH0_INNER    (3UL << 12)
#define TCR_EPD1         (1UL << 23)
#define TCR_IPS_40BIT    (2UL << 32)

#define VIRT_UART_BASE   0x09000000UL
#define VIRT_DRAM_FALLBACK_BASE 0x40000000UL
#define VIRT_DRAM_FALLBACK_SIZE 0x40000000UL

#define L1_INDEX(va)     (((va) >> 30) & 0x1FFUL)
#define L2_INDEX(va)     (((va) >> 21) & 0x1FFUL)

#define MMIO_L2_POOL_MAX 8
#define DEVICE_BLOCK_MAX 64

/*
 * Boot tables. They live in the kernel image and back TTBR0 until the first
 * process root is activated; they never hold EL0 mappings.
 */
static uint64_t g_l0[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l1[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l2_dram[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l2_mmio[MMIO_L2_POOL_MAX][PTE_ENTRIES]
	__attribute__((aligned(PAGE_SIZE)));
static int g_l2_mmio_l1[MMIO_L2_POOL_MAX];

/* Every Device block installed so far, replayed into each user root. */
static uint64_t g_device_blocks[DEVICE_BLOCK_MAX];
static unsigned g_device_count;
/* Device count when the first user root was built (-1: none yet). */
static int g_device_count_at_first_root = -1;

static int g_mmu_on;
static uint64_t g_dram_base = VIRT_DRAM_FALLBACK_BASE;
static uint64_t g_dram_end = VIRT_DRAM_FALLBACK_BASE + VIRT_DRAM_FALLBACK_SIZE;

static uint64_t pte_table(uint64_t table_pa)
{
	return PTE_TYPE_TABLE | (table_pa & PTE_ADDR_MASK);
}

static uint64_t pte_block_device(uint64_t pa)
{
	return PTE_TYPE_BLOCK | ((uint64_t)ATTR_DEVICE << PTE_ATTR_SHIFT) |
	       PTE_AF | PTE_UXN | PTE_PXN | (pa & PTE_BLOCK_MASK);
}

static uint64_t pte_block_dram(uint64_t pa)
{
	/* EL1 RW, kernel-executable, never executable at EL0. */
	return PTE_TYPE_BLOCK | ((uint64_t)ATTR_NORMAL << PTE_ATTR_SHIFT) |
	       PTE_SH_INNER | PTE_AF | PTE_UXN | (pa & PTE_BLOCK_MASK);
}

static void zero_table(uint64_t *tbl)
{
	unsigned i;

	for (i = 0; i < PTE_ENTRIES; i++)
		tbl[i] = 0;
}

static void tlb_flush_all(void)
{
	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("tlbi vmalle1" ::: "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

/*
 * The boot map owns one L2 table for DRAM (one 1 GiB L1 slot). Accept the
 * firmware range only when it fits that window; otherwise keep the QEMU virt
 * fallback used by DTB-less test boots.
 */
static void select_dram_window(void)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();
	uint64_t base;
	uint64_t size;
	uint64_t end;

	g_dram_base = VIRT_DRAM_FALLBACK_BASE;
	g_dram_end = VIRT_DRAM_FALLBACK_BASE + VIRT_DRAM_FALLBACK_SIZE;
	if (!boot || !boot->fdt_valid || boot->memory_range_count == 0U)
		return;
	base = boot->memory[0].base;
	size = boot->memory[0].size;
	end = base + size;
	if (size < BLOCK_2M || end < base || (base & (BLOCK_2M - 1U)) != 0 ||
	    L1_INDEX(base) != L1_INDEX(end - 1U))
		return;
	g_dram_base = base;
	g_dram_end = end & ~(BLOCK_2M - 1U);
}

static uint64_t *mmio_l2_for_l1(uint64_t l1_idx)
{
	unsigned i;
	int free_slot = -1;

	for (i = 0; i < MMIO_L2_POOL_MAX; i++)
	{
		if (g_l2_mmio_l1[i] == (int)l1_idx)
			return g_l2_mmio[i];
		if (free_slot < 0 && g_l2_mmio_l1[i] < 0)
			free_slot = (int)i;
	}
	if (free_slot < 0)
		return NULL;
	g_l2_mmio_l1[free_slot] = (int)l1_idx;
	zero_table(g_l2_mmio[free_slot]);
	return g_l2_mmio[free_slot];
}

static int device_block_recorded(uint64_t block)
{
	unsigned i;

	for (i = 0; i < g_device_count; i++)
		if (g_device_blocks[i] == block)
			return 1;
	return 0;
}

static int install_device_block(uint64_t block)
{
	uint64_t l1_idx = L1_INDEX(block);
	uint64_t *l2;
	uint64_t l2_pa;

	if (device_block_recorded(block))
		return 0;
	if (g_device_count >= DEVICE_BLOCK_MAX)
		return -ENOSPC;

	/*
	 * Raspberry boards place peripheral MMIO in the same 1 GiB L1 window as
	 * RAM. Reuse the DRAM L2 only outside the firmware RAM extent.
	 */
	if (l1_idx == L1_INDEX(g_dram_base))
	{
		if (block < g_dram_end && block + BLOCK_2M > g_dram_base)
			return -EINVAL;
		l2 = g_l2_dram;
	}
	else
		l2 = mmio_l2_for_l1(l1_idx);
	if (!l2)
		return -ENOSPC;
	l2_pa = (uint64_t)(uintptr_t)l2;
	if (g_l1[l1_idx] != 0U &&
	    ((g_l1[l1_idx] & PTE_TYPE_TABLE) != PTE_TYPE_TABLE ||
	     (g_l1[l1_idx] & PTE_ADDR_MASK) != (l2_pa & PTE_ADDR_MASK)))
		return -EINVAL;
	g_l1[l1_idx] = pte_table(l2_pa);
	l2[L2_INDEX(block)] = pte_block_device(block);
	g_device_blocks[g_device_count++] = block;
	return 0;
}

static void build_idmap(void)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();
	uint64_t uart_block = VIRT_UART_BASE & ~(BLOCK_2M - 1U);
	uint64_t block;
	unsigned i;

	zero_table(g_l0);
	zero_table(g_l1);
	zero_table(g_l2_dram);
	for (i = 0; i < MMIO_L2_POOL_MAX; i++)
		g_l2_mmio_l1[i] = -1;
	g_device_count = 0;

	select_dram_window();
	if (boot && boot->fdt_valid && boot->console_uart == ARM64_UART_PL011 &&
	    boot->console_mmio.size != 0U)
		uart_block = boot->console_mmio.base & ~(BLOCK_2M - 1U);

	for (block = g_dram_base; block < g_dram_end; block += BLOCK_2M)
		g_l2_dram[L2_INDEX(block)] = pte_block_dram(block);
	g_l1[L1_INDEX(g_dram_base)] = pte_table((uint64_t)(uintptr_t)g_l2_dram);
	g_l0[0] = pte_table((uint64_t)(uintptr_t)g_l1);
	(void)install_device_block(uart_block);
}

static void mmu_configure_and_enable(uint64_t ttbr0)
{
	uint64_t mair;
	uint64_t tcr;
	uint64_t sctlr;

	mair = (MAIR_ATTR_DEVICE << (8 * ATTR_DEVICE)) |
	       (MAIR_ATTR_NORMAL << (8 * ATTR_NORMAL)) |
	       (MAIR_ATTR_NORMAL_NC << (8 * ATTR_NORMAL_NC));
	__asm__ volatile("msr mair_el1, %0" :: "r"(mair) : "memory");

	tcr = TCR_T0SZ_48 | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_INNER |
	      TCR_EPD1 | TCR_IPS_40BIT;
	__asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");

	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0) : "memory");
	__asm__ volatile("isb" ::: "memory");

	tlb_flush_all();

	/* M (MMU), C (data cache), I (instruction cache); clear WXN and EE. */
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr |= (1UL << 0);
	sctlr |= (1UL << 2);
	sctlr |= (1UL << 12);
	sctlr &= ~(1UL << 25);
	sctlr &= ~(1UL << 19);
	__asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

int arm64_mmu_enable(void)
{
	uint64_t el;

	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	if (((el >> 2) & 3UL) != 1UL)
		return -EINVAL;

	build_idmap();
	mmu_configure_and_enable((uint64_t)(uintptr_t)g_l0);
	g_mmu_on = 1;
	return 0;
}

int arm64_mmu_verify(void)
{
	uint64_t ttbr0;
	uint64_t sctlr;

	if (!g_mmu_on)
		return -ENODEV;
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	if ((ttbr0 & PTE_ADDR_MASK) != ((uint64_t)(uintptr_t)g_l0 & PTE_ADDR_MASK))
		return -EINVAL;
	if ((sctlr & 1UL) == 0)
		return -EINVAL;
	return 0;
}

uintptr_t arm64_mmu_kernel_root(void)
{
	return (uintptr_t)g_l0;
}

int arm64_mmu_map_device_block(uint64_t pa)
{
	int ret;

	if (!g_mmu_on)
		return -ENODEV;
	ret = install_device_block(pa & ~(BLOCK_2M - 1U));
	if (ret == 0)
		tlb_flush_all();
	return ret;
}

int arm64_mmu_map_device_range(uint64_t pa, uint64_t size)
{
	uint64_t block;
	uint64_t last;
	int ret;

	if (!g_mmu_on)
		return -ENODEV;
	if (size == 0U || pa + size - 1U < pa)
		return -EINVAL;
	block = pa & ~(BLOCK_2M - 1U);
	last = (pa + size - 1U) & ~(BLOCK_2M - 1U);
	for (;;)
	{
		ret = install_device_block(block);
		if (ret != 0)
			return ret;
		if (block == last)
			break;
		block += BLOCK_2M;
	}
	tlb_flush_all();
	return 0;
}

static int map_device_blocks(address_space_root_t root, unsigned first)
{
	unsigned i;

	for (i = first; i < g_device_count; i++)
	{
		if (map_supervisor_device_2mb(root, g_device_blocks[i],
					      g_device_blocks[i] + BLOCK_2M) != 0)
			return -ENOMEM;
	}
	return 0;
}

int arm64_mmu_map_supervisor(address_space_root_t root)
{
	if (!root || !g_mmu_on)
		return -EINVAL;
	if (g_device_count_at_first_root < 0)
		g_device_count_at_first_root = (int)g_device_count;
	if (map_supervisor_identity_2mb(root, g_dram_base, g_dram_end) != 0)
		return -ENOMEM;
	return map_device_blocks(root, 0);
}

int arm64_mmu_sync_supervisor(address_space_root_t root)
{
	/* Common case: no Device block appeared after the first user root. */
	if (!root || g_device_count_at_first_root < 0 ||
	    g_device_count == (unsigned)g_device_count_at_first_root)
		return 0;
	return map_device_blocks(root, (unsigned)g_device_count_at_first_root);
}
