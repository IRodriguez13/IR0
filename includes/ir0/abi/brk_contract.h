/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: brk_contract.h
 * Description: Linux-compatible initial program break from ELF PT_LOAD (host + kernel)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_ELF_PT_LOAD 1U
#define IR0_USER_PAGE_SIZE 4096U

typedef struct
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_vaddr;
	uint64_t p_memsz;
} ir0_elf_phdr_brk_t;

static inline uintptr_t ir0_page_align_up(uintptr_t addr)
{
	uintptr_t mask = (uintptr_t)IR0_USER_PAGE_SIZE - 1U;

	return (addr + mask) & ~mask;
}

/*
 * Linux program break after exec: page-aligned max(PT_LOAD p_vaddr + p_memsz).
 * Returns 0 when no PT_LOAD segment is present.
 */
static inline uintptr_t ir0_elf_initial_brk_from_phdr(const ir0_elf_phdr_brk_t *phdr,
						      size_t phnum)
{
	uint64_t end = 0;
	size_t i;

	if (!phdr)
		return 0;

	for (i = 0; i < phnum; i++)
	{
		uint64_t seg_end;

		if (phdr[i].p_type != IR0_ELF_PT_LOAD)
			continue;
		seg_end = phdr[i].p_vaddr + phdr[i].p_memsz;
		if (seg_end > end)
			end = seg_end;
	}

	if (end == 0)
		return 0;

	return ir0_page_align_up((uintptr_t)end);
}
