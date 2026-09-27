/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_load_early.c
 * Description: Load embedded musl hello ELF64 into DRAM and drop to EL0 entry.
 */

#include "elf_load_early.h"
#include "elf_image_early.h"
#include "mmu_early.h"
#include "pl011.h"

#include <stdint.h>
#include <ir0/boot_log.h>

#define PAGE_SIZE 4096UL

#ifndef ARM64_MUSL_STACK_TOP
#define ARM64_MUSL_STACK_TOP 0x43180000UL
#endif
#define MUSL_STACK_PAGES 2U
#define SPSR_DAIF_MASKED 0x3c0UL
#define SPSR_MODE_EL0T 0x0UL

extern const uint8_t hello_aarch64_blob[];
extern const uint8_t hello_aarch64_blob_end[];

static int g_musl_wrote;
static int g_musl_mode;

int arm64_musl_hello_wrote(void)
{
	return g_musl_wrote;
}

int arm64_musl_mode(void)
{
	return g_musl_mode;
}

void arm64_musl_note_write(const char *buf, uint64_t len)
{
	static const char expect[] = "IR0_MUSL_AARCH64_HELLO_OK";
	uint64_t i;

	if (!buf || len < sizeof(expect) - 1)
		return;
	for (i = 0; i < sizeof(expect) - 1; i++)
	{
		if (buf[i] != expect[i])
			return;
	}
	g_musl_wrote = 1;
}

static void setup_auxv_stack(uint64_t sp_top)
{
	uint64_t *sp = (uint64_t *)(uintptr_t)(sp_top - 8UL * 4UL);

	sp[0] = 0;
	sp[1] = 0;
	sp[2] = 0;
	sp[3] = 0;
	__asm__ volatile("msr sp_el0, %0" :: "r"(sp) : "memory");
}

void arm64_after_musl(void)
{
	g_musl_mode = 0;
	if (g_musl_wrote)
		ir0_boot_smoke("ARM64_MUSL_HELLO_OK");
	else
		ir0_boot_smoke("ARM64_MUSL_HELLO_FAIL");
	/* Common init handoff contract, backed by the early ARM64 provider. */
	if (arm64_init_handoff_early() != 0)
	{
		extern void arm64_enter_el0(void);

		arm64_enter_el0();
	}
}

static void enable_fp_simd(void)
{
	uint64_t cpacr;

	/* CPACR_EL1.FPEN = 0b11 — don't trap FP/SIMD at EL0/EL1 (musl memset). */
	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr |= (3UL << 20);
	__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

static void enter_musl_el0(uint64_t entry)
{
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL0T;

	enable_fp_simd();
	g_musl_mode = 1;
	setup_auxv_stack(ARM64_MUSL_STACK_TOP);
	ir0_boot_smoke("ARM64_MUSL_EL0_DROP");

	__asm__ volatile(
		"msr	elr_el1, %0\n"
		"msr	spsr_el1, %1\n"
		"isb\n"
		"eret\n"
		:
		: "r"(entry), "r"(spsr)
		: "memory");
	__builtin_unreachable();
}

int arm64_musl_hello_el0(void)
{
	const uint8_t *blob = hello_aarch64_blob;
	uint64_t blob_len = (uint64_t)(hello_aarch64_blob_end - hello_aarch64_blob);
	struct ir0_elf64_image image;
	unsigned i;

	g_musl_wrote = 0;
	g_musl_mode = 0;

	for (i = 0; i < MUSL_STACK_PAGES; i++)
	{
		uint64_t page = ARM64_MUSL_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;

		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
		{
			ir0_boot_smoke("ARM64_MUSL_LOAD_FAIL");
			return -1;
		}
		arm64_elf_zero_early(page, PAGE_SIZE);
	}

	if (arm64_elf_image_load_early(blob, blob_len, &image) != 0)
	{
		ir0_boot_smoke("ARM64_MUSL_LOAD_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_MUSL_LOAD_OK");
	enter_musl_el0(image.entry);
	return 0;
}
