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
#include <ir0/early_elf_stack.h>
#include <ir0/early_syscall.h>

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

static int arm64_early_copy_from_user(void *destination, uint64_t source,
				      uint64_t size)
{
	uint8_t *dst = destination;
	const volatile uint8_t *src;
	uint64_t i;

	if (!destination || size == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(source, size))
		return -1;
	src = (const volatile uint8_t *)(uintptr_t)source;
	for (i = 0; i < size; i++)
		dst[i] = src[i];
	return 0;
}

static int arm64_early_copy_to_user(uint64_t destination, const void *source,
				    uint64_t size)
{
	volatile uint8_t *dst;
	const uint8_t *src = source;
	uint64_t i;

	if (!source || size == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(destination, size))
		return -1;
	dst = (volatile uint8_t *)(uintptr_t)destination;
	for (i = 0; i < size; i++)
		dst[i] = src[i];
	return 0;
}

static const struct early_user_services g_early_user = {
	.copy_from_user = arm64_early_copy_from_user,
	.copy_to_user = arm64_early_copy_to_user,
};

static void enable_fp_simd(void)
{
	uint64_t cpacr;

	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr |= (3UL << 20);
	__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

int arm64_early_elf_enter(uint64_t entry, uint64_t stack_top, uint64_t stack_size,
			  const struct ir0_early_elf_auxv *aux,
			  char *const argv[])
{
	struct ir0_early_elf_stack_params params;
	uint64_t sp;
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL0T;

	if (!aux || entry == 0 || stack_top == 0)
		return -1;

	params.stack_top = stack_top;
	params.stack_size = stack_size;
	params.argv = argv;
	params.envp = 0;
	params.aux = *aux;
	if (!params.aux.execfn && argv && argv[0])
		params.aux.execfn = argv[0];

	enable_fp_simd();
	if (ir0_early_elf_setup_stack(&g_early_user, &params, &sp) != 0)
		return -1;
	__asm__ volatile("msr sp_el0, %0" :: "r"(sp) : "memory");
	__asm__ volatile(
		"msr	elr_el1, %0\n"
		"msr	spsr_el1, %1\n"
		"mov	x0, xzr\n"
		"isb\n"
		"eret\n"
		:
		: "r"(entry), "r"(spsr)
		: "x0", "memory");
	__builtin_unreachable();
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

int arm64_musl_hello_el0(void)
{
	const uint8_t *blob = hello_aarch64_blob;
	uint64_t blob_len = (uint64_t)(hello_aarch64_blob_end - hello_aarch64_blob);
	struct ir0_elf64_image image;
	struct ir0_early_elf_auxv aux;
	char *argv[] = { "hello", 0 };
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

	aux.phdr = image.phdr;
	aux.phent = image.phent;
	aux.phnum = image.phnum;
	aux.pagesz = PAGE_SIZE;
	aux.base = 0;
	aux.flags = 0;
	aux.entry = image.entry;
	aux.uid = 0;
	aux.euid = 0;
	aux.gid = 0;
	aux.egid = 0;
	aux.secure = 0;
	aux.random = 0;
	aux.execfn = argv[0];

	ir0_boot_smoke("ARM64_MUSL_LOAD_OK");
	g_musl_mode = 1;
	ir0_boot_smoke("ARM64_MUSL_EL0_DROP");
	if (arm64_early_elf_enter(image.entry, ARM64_MUSL_STACK_TOP,
				  (uint64_t)MUSL_STACK_PAGES * PAGE_SIZE,
				  &aux, argv) != 0)
	{
		ir0_boot_smoke("ARM64_MUSL_LOAD_FAIL");
		g_musl_mode = 0;
		return -1;
	}
	return 0;
}
