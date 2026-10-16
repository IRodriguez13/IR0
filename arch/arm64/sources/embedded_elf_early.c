/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: embedded_elf_early.c
 * Description: Load the second embedded ET_EXEC and enter with caller argv
 */

#include "elf_load_early.h"
#include "elf_image_early.h"
#include "mmu_early.h"
#include "pl011.h"
#include "rootfs_early.h"

#include <stdint.h>
#include <ir0/boot_log.h>
#include <ir0/early_elf_stack.h>
#include <ir0/early_mm.h>

#define PAGE_SIZE 4096UL

#ifndef ARM64_GUEST1_STACK_TOP
#define ARM64_GUEST1_STACK_TOP 0x441a0000UL
#endif
#ifndef ARM64_GUEST1_MMAP_BASE
#define ARM64_GUEST1_MMAP_BASE 0x44200000UL
#endif
#ifndef ARM64_GUEST1_MMAP_END
#define ARM64_GUEST1_MMAP_END 0x44800000UL
#endif
#define GUEST1_STACK_PAGES 4U

extern const uint8_t embedded_elf_blob[];
extern const uint8_t embedded_elf_blob_end[];

static int g_stage;
static uint64_t g_entry;
static uint64_t g_heap;
static struct ir0_early_elf_auxv g_aux;

static void after_guest1(int wrote)
{
	extern void arm64_enter_el0(void);

	if (g_stage == 0)
	{
		if (wrote)
			ir0_boot_smoke("ARM64_GUEST_OK");
		else
			ir0_boot_smoke("ARM64_GUEST_FAIL");
#ifdef ARM64_SKIP_INIT_HARNESS
		ir0_boot_smoke("ARM64_INIT_DEFERRED");
		arm64_enter_el0();
		return;
#else
		g_stage = 1;
		if (arm64_embedded_init_el0() != 0)
			arm64_enter_el0();
		return;
#endif
	}

	if (wrote)
		ir0_boot_smoke("ARM64_INIT_EL0_OK");
	else
		ir0_boot_smoke("ARM64_INIT_EL0_FAIL");
	arm64_enter_el0();
}

void arm64_embedded_elf_enter(char *const argv[])
{
	int init_stage = g_stage != 0;
	struct early_mm_layout mm;

	if (g_entry == 0 || g_heap == 0)
		return;
	mm.heap_base = g_heap;
	mm.map_base = ARM64_GUEST1_MMAP_BASE;
	mm.map_limit = ARM64_GUEST1_MMAP_END;
	early_mm_activate(&mm);
	arm64_early_guest_begin(after_guest1);
	if (init_stage)
		ir0_boot_smoke("ARM64_INIT_EL0_DROP");
	else
		ir0_boot_smoke("ARM64_GUEST_DROP");
	if (arm64_early_elf_enter(g_entry, ARM64_GUEST1_STACK_TOP,
				  (uint64_t)GUEST1_STACK_PAGES * PAGE_SIZE,
				  &g_aux, argv) != 0)
		ir0_boot_smoke("ARM64_GUEST_LOAD_FAIL");
}

int arm64_embedded_init_el0(void)
{
	char *argv[] = { "echo", "ARM64_EL0_OK", 0 };

	if (g_entry == 0)
		return -1;
	if (arm64_rootfs_smoke_init() != 0)
	{
		ir0_boot_smoke("ARM64_INIT_EL0_FAIL");
		return -1;
	}
	arm64_embedded_elf_enter(argv);
	return 0;
}

int arm64_embedded_elf_prepare(void)
{
	const uint8_t *blob = embedded_elf_blob;
	uint64_t blob_len = (uint64_t)(embedded_elf_blob_end - embedded_elf_blob);
	struct ir0_elf64_image image;
	unsigned i;

	g_stage = 0;
	g_entry = 0;
	g_heap = 0;
	arm64_rootfs_early_init();

	for (i = 0; i < GUEST1_STACK_PAGES; i++)
	{
		uint64_t page = ARM64_GUEST1_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;

		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
		{
			ir0_boot_smoke("ARM64_GUEST_LOAD_FAIL");
			return -1;
		}
		arm64_elf_zero_early(page, PAGE_SIZE);
	}

	if (arm64_elf_image_load_early(blob, blob_len, &image) != 0 ||
	    image.initial_brk == 0)
	{
		ir0_boot_smoke("ARM64_GUEST_LOAD_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_GUEST_LOAD_OK");
	g_entry = image.entry;
	g_heap = image.initial_brk;
	g_aux.phdr = image.phdr;
	g_aux.phent = image.phent;
	g_aux.phnum = image.phnum;
	g_aux.pagesz = PAGE_SIZE;
	g_aux.base = 0;
	g_aux.flags = 0;
	g_aux.entry = image.entry;
	g_aux.uid = 0;
	g_aux.euid = 0;
	g_aux.gid = 0;
	g_aux.egid = 0;
	g_aux.secure = 0;
	g_aux.random = 0;
	g_aux.execfn = 0;
	return 0;
}

int arm64_embedded_elf_el0(void)
{
	char *argv[] = { "echo", "ARM64_EL0_OK", 0 };

	if (arm64_embedded_elf_prepare() != 0)
		return -1;
	arm64_embedded_elf_enter(argv);
	return -1;
}
