/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: busybox_load_early.c
 * Description: Load the embedded PID1 ELF blob and enter with caller argv
 */

#include "elf_load_early.h"
#include "elf_image_early.h"
#include "mmu_early.h"
#include "pl011.h"
#include "rootfs_early.h"
#include "syscall_early.h"

#include <stdint.h>
#include <ir0/boot_log.h>
#include <ir0/early_elf_stack.h>

#define PAGE_SIZE 4096UL

#ifndef ARM64_BB_STACK_TOP
#define ARM64_BB_STACK_TOP 0x441a0000UL
#endif
#define BB_STACK_PAGES 4U

extern const uint8_t busybox_aarch64_blob[];
extern const uint8_t busybox_aarch64_blob_end[];

static int g_bb_mode;
static int g_bb_wrote;
static int g_bb_init_wrote;
static int g_bb_stage;
static uint64_t g_bb_entry;
static struct ir0_early_elf_auxv g_bb_aux;

int arm64_busybox_mode(void)
{
	return g_bb_mode;
}

int arm64_busybox_wrote(void)
{
	return g_bb_wrote;
}

void arm64_busybox_note_write(const char *buf, uint64_t len)
{
	static const char expect_el0[] = "ARM64_BUSYBOX_EL0_OK";
	static const char expect_init[] = "ARM64_BUSYBOX_INIT_OK";
	uint64_t i;

	if (!buf)
		return;

	if (g_bb_stage == 0)
	{
		if (len < sizeof(expect_el0) - 1)
			return;
		for (i = 0; i < sizeof(expect_el0) - 1; i++)
		{
			if (buf[i] != expect_el0[i])
				return;
		}
		g_bb_wrote = 1;
		return;
	}

	if (len < sizeof(expect_init) - 1)
		return;
	for (i = 0; i < sizeof(expect_init) - 1; i++)
	{
		if (buf[i] != expect_init[i])
			return;
	}
	g_bb_init_wrote = 1;
}

void arm64_after_busybox(void)
{
	extern void arm64_enter_el0(void);

	g_bb_mode = 0;
	if (g_bb_stage == 0)
	{
		if (g_bb_wrote)
			ir0_boot_smoke("ARM64_BUSYBOX_EL0_OK");
		else
			ir0_boot_smoke("ARM64_BUSYBOX_EL0_FAIL");
#ifdef ARM64_BUSYBOX_SKIP_INIT_HARNESS
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_DEFERRED");
		arm64_enter_el0();
		return;
#else
		g_bb_stage = 1;
		if (arm64_busybox_init_el0() != 0)
			arm64_enter_el0();
		return;
#endif
	}

	if (g_bb_init_wrote)
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_OK");
	else
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_FAIL");
	arm64_enter_el0();
}

void arm64_busybox_enter(char *const argv[])
{
	int init_stage = g_bb_stage != 0;

	if (g_bb_entry == 0)
		return;
	arm64_syscall_reset_busybox_heap();
	g_bb_mode = 1;
	if (init_stage)
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_EL0_DROP");
	else
		ir0_boot_smoke("ARM64_BUSYBOX_EL0_DROP");
	if (arm64_early_elf_enter(g_bb_entry, ARM64_BB_STACK_TOP,
				  (uint64_t)BB_STACK_PAGES * PAGE_SIZE,
				  &g_bb_aux, argv) != 0)
		ir0_boot_smoke("ARM64_BUSYBOX_LOAD_FAIL");
}

int arm64_busybox_init_el0(void)
{
	/*
	 * Harness vector only: the second drop still needs an applet that
	 * can run without clone/execve. The loader does not invent this.
	 */
	char *argv[] = { "echo", "ARM64_BUSYBOX_INIT_OK", 0 };

	if (g_bb_entry == 0)
		return -1;
	if (arm64_rootfs_smoke_init() != 0)
	{
		ir0_boot_smoke("ARM64_BUSYBOX_INIT_FAIL");
		return -1;
	}
	g_bb_init_wrote = 0;
	arm64_busybox_enter(argv);
	return 0;
}

int arm64_busybox_prepare(void)
{
	const uint8_t *blob = busybox_aarch64_blob;
	uint64_t blob_len = (uint64_t)(busybox_aarch64_blob_end - busybox_aarch64_blob);
	struct ir0_elf64_image image;
	unsigned i;

	g_bb_wrote = 0;
	g_bb_init_wrote = 0;
	g_bb_stage = 0;
	g_bb_mode = 0;
	arm64_rootfs_early_init();

	for (i = 0; i < BB_STACK_PAGES; i++)
	{
		uint64_t page = ARM64_BB_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;

		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
		{
			ir0_boot_smoke("ARM64_BUSYBOX_LOAD_FAIL");
			return -1;
		}
		arm64_elf_zero_early(page, PAGE_SIZE);
	}

	if (arm64_elf_image_load_early(blob, blob_len, &image) != 0)
	{
		ir0_boot_smoke("ARM64_BUSYBOX_LOAD_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_BUSYBOX_LOAD_OK");
	g_bb_entry = image.entry;
	g_bb_aux.phdr = image.phdr;
	g_bb_aux.phent = image.phent;
	g_bb_aux.phnum = image.phnum;
	g_bb_aux.pagesz = PAGE_SIZE;
	g_bb_aux.base = 0;
	g_bb_aux.flags = 0;
	g_bb_aux.entry = image.entry;
	g_bb_aux.uid = 0;
	g_bb_aux.euid = 0;
	g_bb_aux.gid = 0;
	g_bb_aux.egid = 0;
	g_bb_aux.secure = 0;
	g_bb_aux.random = 0;
	g_bb_aux.execfn = 0;
	return 0;
}

int arm64_busybox_el0(void)
{
	char *argv[] = { "echo", "ARM64_BUSYBOX_EL0_OK", 0 };

	if (arm64_busybox_prepare() != 0)
		return -1;
	arm64_busybox_enter(argv);
	return -1;
}
