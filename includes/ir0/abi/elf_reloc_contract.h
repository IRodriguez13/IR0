/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: elf_reloc_contract.h
 * Description: Local ET_EXEC DT_RELA value (tcc GLOB_DAT without ld.so)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_R_X86_64_64        1U
#define IR0_R_X86_64_GLOB_DAT  6U
#define IR0_R_X86_64_JUMP_SLOT 7U
#define IR0_R_X86_64_RELATIVE  8U

/*
 * Resolve one x86-64 reloc that names a symbol already in the same ET_EXEC
 * image. load_bias is 0 for ET_EXEC. Returns 0 and writes *out, or -1 if the
 * type is not a local reloc (caller skips).
 *
 * Linux applies these in ld.so. IR0 has no PT_INTERP yet; tcc still emits
 * R_X86_64_GLOB_DAT for __environ / main / _init in a self-contained binary.
 */
static inline int ir0_elf64_local_reloc_value(uint32_t type, uint64_t load_bias,
					      uint64_t sym_value, int64_t addend,
					      uint64_t *out)
{
	if (!out)
		return -1;

	switch (type)
	{
	case IR0_R_X86_64_RELATIVE:
		*out = load_bias + (uint64_t)addend;
		return 0;
	case IR0_R_X86_64_64:
	case IR0_R_X86_64_GLOB_DAT:
	case IR0_R_X86_64_JUMP_SLOT:
		*out = load_bias + sym_value + (uint64_t)addend;
		return 0;
	default:
		return -1;
	}
}
