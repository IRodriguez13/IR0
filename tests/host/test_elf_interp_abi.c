/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_elf_interp_abi.c
 * Description: Host checks for PT_INTERP path + AT_BASE / interp entry
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/abi/elf_interp_contract.h>
#include <stdint.h>
#include <string.h>

void test_elf_interp_abi(void)
{
	uint64_t out = 0;
	char path[64];
	uint8_t img[256];
	const char *interp = "/lib/ld64.so.1";

	TEST_BEGIN("elf_interp_abi");

	ASSERT_EQ(ir0_elf64_interp_entry(3, 0x7bf0eULL, IR0_ELF_INTERP_BASE, &out), 0);
	ASSERT_EQ(out, IR0_ELF_INTERP_BASE + 0x7bf0eULL);

	ASSERT_EQ(ir0_elf64_interp_entry(2, 0x401000ULL, IR0_ELF_INTERP_BASE, &out), 0);
	ASSERT_EQ(out, 0x401000ULL);

	ASSERT_EQ(ir0_elf64_interp_at_base(0, IR0_ELF_INTERP_BASE, &out), 0);
	ASSERT_EQ(out, IR0_ELF_INTERP_BASE);

	memset(img, 0, sizeof(img));
	img[0] = 0x7f;
	img[1] = 'E';
	img[2] = 'L';
	img[3] = 'F';
	img[4] = 2;
	{
		uint64_t phoff = 64;
		uint16_t phentsize = 56;
		uint16_t phnum = 1;
		uint32_t p_type = IR0_PT_INTERP;
		uint64_t p_offset = 128;
		uint64_t p_filesz = 15;

		memcpy(img + 32, &phoff, sizeof(phoff));
		memcpy(img + 54, &phentsize, sizeof(phentsize));
		memcpy(img + 56, &phnum, sizeof(phnum));
		memcpy(img + 64, &p_type, sizeof(p_type));
		memcpy(img + 64 + 8, &p_offset, sizeof(p_offset));
		memcpy(img + 64 + 32, &p_filesz, sizeof(p_filesz));
		memcpy(img + 128, interp, 15);
	}
	memset(path, 0, sizeof(path));
	ASSERT_EQ(ir0_elf64_read_interp_path(img, sizeof(img), path, sizeof(path)), 0);
	ASSERT_EQ(strcmp(path, interp), 0);

	{
		uint32_t p_type = 1; /* PT_LOAD */

		memcpy(img + 64, &p_type, sizeof(p_type));
	}
	ASSERT_EQ(ir0_elf64_read_interp_path(img, sizeof(img), path, sizeof(path)), 1);

	TEST_END();
}
