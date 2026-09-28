/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_policy.c
 * Description: AArch64 ELF machine and relocation policy backend.
 */

#include <ir0/arch_elf.h>

#define ELF_EM_AARCH64       183U
#define R_AARCH64_ABS64      257U
#define R_AARCH64_GLOB_DAT   1025U
#define R_AARCH64_JUMP_SLOT  1026U
#define R_AARCH64_RELATIVE   1027U

int elf_machine_supported(uint16_t machine)
{
	return machine == ELF_EM_AARCH64;
}

int elf_reloc_is_relative(uint32_t type)
{
	return type == R_AARCH64_RELATIVE;
}

int elf_local_reloc_value(uint32_t type, uint64_t load_bias,
			  uint64_t sym_value, int64_t addend,
			  uint64_t *out)
{
	if (!out)
		return -1;

	switch (type)
	{
	case R_AARCH64_RELATIVE:
		*out = load_bias + (uint64_t)addend;
		return 0;
	case R_AARCH64_ABS64:
	case R_AARCH64_GLOB_DAT:
	case R_AARCH64_JUMP_SLOT:
		*out = load_bias + sym_value + (uint64_t)addend;
		return 0;
	default:
		return -1;
	}
}
