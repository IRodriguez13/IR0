/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmu.c
 * Description: ARM64 kernel MMU. The kernel lives in TTBR1 only: the image
 *              (entry.S), the linear map (mm/paging/direct_map.c) and the
 *              ioremap window for device registers and framebuffers.
 *              TTBR0 holds user mappings; before the first process it
 *              holds the boot identity of DRAM. User mappings are owned by
 *              the common page-table walker (mm/paging/paging.c) through
 *              the mm_ops descriptor encoders.
 *
 * Reference: Linux arch/arm64/kernel/head.S (idmap), arch/arm64/mm/proc.S
 * (TCR/MAIR). entry.S turns the MMU on with the image in TTBR1 at
 * KIMAGE_VADDR; this file owns the TTBR0 boot identity and the linear map.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mmu.h"

#include <arch/common/arch_portable.h>
#include <arm64_board.h>
#include <ir0/errno.h>
#include <ir0/io.h>
#include <ir0/page.h>
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
/* MAIR_EL1 itself is programmed by entry.S (MAIR_INIT). */

#define VIRT_DRAM_FALLBACK_BASE 0x40000000UL
#define VIRT_DRAM_FALLBACK_SIZE 0x40000000UL

#define L1_INDEX(va)     (((va) >> 30) & 0x1FFUL)
#define L2_INDEX(va)     (((va) >> 21) & 0x1FFUL)

#define MMIO_L2_POOL_MAX 8
/* L3 tables for write-combining pages: 8 x 2 MiB covers 1920x1080x32. */
#define WC_L3_POOL_MAX   8

/*
 * ioremap window (TTBR1): one L0 slot, 512 GiB, where the device or
 * framebuffer at physical address PA is reached at IOREMAP_BASE + PA.
 */
#define IOREMAP_BASE     0xfffffc0000000000UL
#define IOREMAP_SIZE     (1UL << 39)
#define IOREMAP_L0_INDEX 504U

/*
 * Boot identity of DRAM: TTBR0 from arm64_mmu_enable() until the first
 * process root, and for threads without a user address space. It never
 * holds EL0 mappings.
 */
static uint64_t g_l0[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l1[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_l2_dram[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

/* ioremap window tables; a pool slot is taken when its 1 GiB is first used. */
static uint64_t g_io_l1[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t g_io_l2[MMIO_L2_POOL_MAX][PTE_ENTRIES]
	__attribute__((aligned(PAGE_SIZE)));
static uint64_t g_io_l3[WC_L3_POOL_MAX][PTE_ENTRIES]
	__attribute__((aligned(PAGE_SIZE)));
static unsigned g_io_l2_used;
static unsigned g_io_l3_used;

/*
 * TTBR1 root (entry.S): the kernel image at KIMAGE_VADDR from the first
 * instruction with the MMU on, then the linear map of all RAM and the
 * ioremap window.
 */
extern uint64_t swapper_pg_dir[PTE_ENTRIES];

uint64_t kimage_voffset;

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

static uint64_t pte_page_wc(uint64_t pa)
{
	/* L3 page descriptor (bits[1:0] = 0b11), Normal non-cacheable. */
	return PTE_TYPE_TABLE | ((uint64_t)ATTR_NORMAL_NC << PTE_ATTR_SHIFT) |
	       PTE_SH_INNER | PTE_AF | PTE_UXN | PTE_PXN | (pa & PTE_ADDR_MASK);
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

static void build_idmap(void)
{
	uint64_t block;

	zero_table(g_l0);
	zero_table(g_l1);
	zero_table(g_l2_dram);
	select_dram_window();
	for (block = g_dram_base; block < g_dram_end; block += BLOCK_2M)
		g_l2_dram[L2_INDEX(block)] = pte_block_dram(block);
	g_l1[L1_INDEX(g_dram_base)] = pte_table(virt_to_phys(g_l2_dram));
	g_l0[0] = pte_table(virt_to_phys(g_l1));
}

/*
 * entry.S already enabled the MMU (MAIR, TCR, both TTBRs, SCTLR). Replace
 * its Device-only boot identity with the DRAM identity map.
 */
static void mmu_install_idmap(uint64_t ttbr0)
{
	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0) : "memory");
	__asm__ volatile("isb" ::: "memory");
	tlb_flush_all();
}

int arm64_mmu_enable(void)
{
	uint64_t el;

	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	if (((el >> 2) & 3UL) != 1UL)
		return -EINVAL;

	build_idmap();
	mmu_install_idmap(virt_to_phys(g_l0));
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
	if ((ttbr0 & PTE_ADDR_MASK) != (virt_to_phys(g_l0) & PTE_ADDR_MASK))
		return -EINVAL;
	if ((sctlr & 1UL) == 0)
		return -EINVAL;
	return 0;
}

uint64_t *mm_kernel_map_root(void)
{
	return g_mmu_on ? swapper_pg_dir : NULL;
}

/*
 * TTBR1 walks are on since entry.S; publish the linear map just added to
 * swapper_pg_dir (Linux arm64 swapper_pg_dir). EL0 never reaches it: its
 * descriptors carry no AP[1] user permission.
 */
void mm_kernel_map_enable(void)
{
	tlb_flush_all();
}

uintptr_t arm64_mmu_kernel_root(void)
{
	return virt_to_phys(g_l0);
}

/* ioremap tables are image statics: reach a descriptor's table there. */
static uint64_t *kimage_table(uint64_t desc)
{
	return (uint64_t *)(uintptr_t)((desc & PTE_ADDR_MASK) + kimage_voffset);
}

/* L2 table of the ioremap window that covers @pa, created on first use. */
static uint64_t *io_l2_for(uint64_t pa)
{
	uint64_t idx = L1_INDEX(pa);
	uint64_t *l2;

	if (swapper_pg_dir[IOREMAP_L0_INDEX] == 0U)
		swapper_pg_dir[IOREMAP_L0_INDEX] = pte_table(virt_to_phys(g_io_l1));
	if (g_io_l1[idx] != 0U)
		return kimage_table(g_io_l1[idx]);
	if (g_io_l2_used >= MMIO_L2_POOL_MAX)
		return NULL;
	l2 = g_io_l2[g_io_l2_used++];
	zero_table(l2);
	g_io_l1[idx] = pte_table(virt_to_phys(l2));
	return l2;
}

static int io_range_ok(uintptr_t pa, size_t size)
{
	if (size == 0U || pa + size - 1U < pa)
		return 0;
	return pa + size - 1U < IOREMAP_SIZE;
}

static void io_publish(void)
{
	/* New entries replace invalid ones: no stale TLB entry can exist. */
	__asm__ volatile("dsb ishst" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

/*
 * Device-nGnRnE 2 MiB blocks over [pa, pa + size). Asking again for a
 * range already mapped returns the same address.
 */
void *ioremap(uintptr_t pa, size_t size)
{
	uint64_t block;
	uint64_t last;

	if (!io_range_ok(pa, size))
		return NULL;
	block = pa & ~(BLOCK_2M - 1U);
	last = (pa + size - 1U) & ~(BLOCK_2M - 1U);
	for (;;)
	{
		uint64_t *l2 = io_l2_for(block);
		uint64_t *slot;

		if (!l2)
			return NULL;
		slot = &l2[L2_INDEX(block)];
		if (*slot == 0U)
			*slot = pte_block_device(block);
		else if (*slot != pte_block_device(block))
			return NULL;
		if (block == last)
			break;
		block += BLOCK_2M;
	}
	io_publish();
	return (void *)(IOREMAP_BASE + pa);
}

/*
 * Normal non-cacheable 4 KiB pages over [pa, pa + size): scanout memory,
 * which the display reads behind the CPU caches.
 */
void *ioremap_wc(uintptr_t pa, size_t size)
{
	uint64_t page;
	uint64_t end;

	if (!io_range_ok(pa, size))
		return NULL;
	end = (pa + size + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
	for (page = pa & ~(PAGE_SIZE - 1U); page < end; page += PAGE_SIZE)
	{
		uint64_t *l2 = io_l2_for(page);
		uint64_t *l3;
		uint64_t *slot;

		if (!l2)
			return NULL;
		slot = &l2[L2_INDEX(page)];
		if (*slot == 0U)
		{
			if (g_io_l3_used >= WC_L3_POOL_MAX)
				return NULL;
			l3 = g_io_l3[g_io_l3_used++];
			zero_table(l3);
			*slot = pte_table(virt_to_phys(l3));
		}
		else if ((*slot & PTE_TYPE_TABLE) != PTE_TYPE_TABLE)
			return NULL;
		l3 = kimage_table(*slot);
		l3[(page >> 12) & 0x1FFUL] = pte_page_wc(page);
	}
	io_publish();
	return (void *)(IOREMAP_BASE + pa);
}
