/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_elf_local_reloc_abi.c
 * Description: Host checks for ISA-selected ELF machine and relocation policy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stdint.h>

int x86_test_elf_machine_supported(uint16_t machine);
int x86_test_elf_reloc_is_relative(uint32_t type);
int x86_test_elf_local_reloc_value(uint32_t type, uint64_t load_bias,
				   uint64_t sym_value, int64_t addend,
				   uint64_t *out);
int arm64_test_elf_machine_supported(uint16_t machine);
int arm64_test_elf_reloc_is_relative(uint32_t type);
int arm64_test_elf_local_reloc_value(uint32_t type, uint64_t load_bias,
				     uint64_t sym_value, int64_t addend,
				     uint64_t *out);

void test_elf_local_reloc_abi(void)
{
	uint64_t out = 0xdeadULL;

	TEST_BEGIN("elf_local_reloc_abi");

	/* Guest hello: R_X86_64_GLOB_DAT __environ @ 0x406210 → 0x407828 */
	ASSERT(x86_test_elf_machine_supported(62));
	ASSERT(!x86_test_elf_machine_supported(183));
	ASSERT(x86_test_elf_reloc_is_relative(8));
	ASSERT_EQ(x86_test_elf_local_reloc_value(6, 0, 0x407828ULL, 0, &out), 0);
	ASSERT_EQ(out, 0x407828ULL);

	ASSERT_EQ(x86_test_elf_local_reloc_value(8, 0, 0, 0x401000, &out), 0);
	ASSERT_EQ(out, 0x401000ULL);

	ASSERT_EQ(x86_test_elf_local_reloc_value(99U, 0, 1, 0, &out), -1);

	ASSERT(arm64_test_elf_machine_supported(183));
	ASSERT(!arm64_test_elf_machine_supported(62));
	ASSERT(arm64_test_elf_reloc_is_relative(1027));
	ASSERT_EQ(arm64_test_elf_local_reloc_value(1025, 0x400000ULL,
						   0x2800ULL, 4, &out), 0);
	ASSERT_EQ(out, 0x402804ULL);
	ASSERT_EQ(arm64_test_elf_local_reloc_value(1027, 0x400000ULL,
						   0, 0x1000, &out), 0);
	ASSERT_EQ(out, 0x401000ULL);

	TEST_END();
}
