/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_elf_local_reloc_abi.c
 * Description: Host checks for tcc ET_EXEC GLOB_DAT local reloc values
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/abi/elf_reloc_contract.h>
#include <stdint.h>

void test_elf_local_reloc_abi(void)
{
	uint64_t out = 0xdeadULL;

	TEST_BEGIN("elf_local_reloc_abi");

	/* Guest hello: R_X86_64_GLOB_DAT __environ @ 0x406210 → 0x407828 */
	ASSERT_EQ(ir0_elf64_local_reloc_value(IR0_R_X86_64_GLOB_DAT, 0,
					      0x407828ULL, 0, &out), 0);
	ASSERT_EQ(out, 0x407828ULL);

	ASSERT_EQ(ir0_elf64_local_reloc_value(IR0_R_X86_64_RELATIVE, 0,
					      0, 0x401000, &out), 0);
	ASSERT_EQ(out, 0x401000ULL);

	ASSERT_EQ(ir0_elf64_local_reloc_value(99U, 0, 1, 0, &out), -1);

	TEST_END();
}
