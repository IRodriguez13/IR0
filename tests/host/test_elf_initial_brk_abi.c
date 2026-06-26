/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_elf_initial_brk_abi.c
 * Description: Host ABI checks for Linux initial program break from PT_LOAD
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/abi/brk_contract.h>
#include <stddef.h>
#include <stdint.h>

void test_elf_initial_brk_abi(void)
{
	static const ir0_elf_phdr_brk_t busybox_ph[] = {
		{ IR0_ELF_PT_LOAD, 4, 0x00400000ULL, 0x190ULL },
		{ IR0_ELF_PT_LOAD, 5, 0x00401000ULL, 0x413edULL },
		{ IR0_ELF_PT_LOAD, 4, 0x00443000ULL, 0xcbd4ULL },
		{ IR0_ELF_PT_LOAD, 6, 0x00450e70ULL, 0x1bd8ULL },
	};
	uintptr_t brk;

	TEST_BEGIN("elf_initial_brk_abi");

	ASSERT_EQ(ir0_page_align_up(0x452a48ULL), 0x453000ULL);
	brk = ir0_elf_initial_brk_from_phdr(busybox_ph,
					  sizeof(busybox_ph) /
						  sizeof(busybox_ph[0]));
	ASSERT_EQ(brk, 0x453000ULL);

	brk = ir0_elf_initial_brk_from_phdr(NULL, 4);
	ASSERT_EQ(brk, 0U);

	TEST_END();
}
