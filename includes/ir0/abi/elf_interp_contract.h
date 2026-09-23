/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: elf_interp_contract.h
 * Description: PT_INTERP path + AT_BASE / interp entry (Linux binfmt_elf)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IR0_PT_INTERP 3U
#define IR0_ELF_INTERP_BASE 0x40000000ULL
#define IR0_ELF_INTERP_PATH_MAX 255U
#define IR0_ELF_INTERP_MAX_PHNUM 64U

/*
 * Linux fs/binfmt_elf.c: AT_ENTRY is the main e_entry; RIP is interp
 * e_entry + load bias for ET_DYN. AT_BASE is the interpreter load address.
 */
static inline int ir0_elf64_interp_entry(uint16_t e_type, uint64_t e_entry,
					 uint64_t load_bias, uint64_t *out)
{
	if (!out)
		return -1;
	/* ET_DYN = 3: e_entry is relative. ET_EXEC keeps the absolute VA. */
	*out = (e_type == 3U) ? (load_bias + e_entry) : e_entry;
	return 0;
}

static inline int ir0_elf64_interp_at_base(uint64_t min_vaddr, uint64_t load_bias,
					   uint64_t *out)
{
	if (!out)
		return -1;
	*out = load_bias + min_vaddr;
	return 0;
}

/*
 * Copy the PT_INTERP C string from an in-memory ELF64 image.
 * Returns 0 if found, 1 if the image has no PT_INTERP, -1 if malformed.
 */
static inline int ir0_elf64_read_interp_path(const uint8_t *file, size_t file_size,
					     char *out, size_t out_sz)
{
	uint64_t phoff;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t i;

	if (!file || !out || out_sz < 2 || file_size < 64)
		return -1;

	memcpy(&phoff, file + 32, sizeof(phoff));
	memcpy(&phentsize, file + 54, sizeof(phentsize));
	memcpy(&phnum, file + 56, sizeof(phnum));
	if (phentsize < 56 || phnum == 0 || phnum > IR0_ELF_INTERP_MAX_PHNUM)
		return -1;
	if (phoff > file_size)
		return -1;

	for (i = 0; i < phnum; i++)
	{
		uint64_t poff = phoff + (uint64_t)i * (uint64_t)phentsize;
		uint32_t p_type;
		uint64_t p_offset;
		uint64_t p_filesz;
		size_t n;
		size_t j;

		if (poff + 40 > file_size)
			return -1;
		memcpy(&p_type, file + poff, sizeof(p_type));
		if (p_type != IR0_PT_INTERP)
			continue;
		memcpy(&p_offset, file + poff + 8, sizeof(p_offset));
		memcpy(&p_filesz, file + poff + 32, sizeof(p_filesz));
		if (p_filesz < 2 || p_filesz > IR0_ELF_INTERP_PATH_MAX)
			return -1;
		if (p_offset >= file_size || p_filesz > file_size - p_offset)
			return -1;
		if (p_filesz >= out_sz)
			return -1;
		memcpy(out, file + p_offset, (size_t)p_filesz);
		out[p_filesz] = '\0';
		n = 0;
		while (n < (size_t)p_filesz && out[n] != '\0')
			n++;
		if (n == 0 || out[0] != '/')
			return -1;
		for (j = 1; j < n; j++)
		{
			if (out[j] == '\0')
				return -1;
		}
		return 0;
	}
	return 1;
}
