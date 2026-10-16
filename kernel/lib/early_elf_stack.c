/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_elf_stack.c
 * Description: Architecture-neutral staged SysV ELF entry stack
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <ir0/early_elf_stack.h>

#define EARLY_ELF_AUXV_PAIRS 15U

static const uint8_t g_default_random[IR0_EARLY_ELF_AT_RANDOM_BYTES] = {
	0x49, 0x31, 0x6d, 0x6f, 0x64, 0x6e, 0x70, 0x72,
	0x31, 0x2e, 0x30, 0x2e, 0x30, 0x31, 0x52, 0x49,
};

static unsigned cstr_len(const char *s)
{
	unsigned n;

	if (!s)
		return 0;
	for (n = 0; s[n]; n++)
		;
	return n;
}

static unsigned count_vec(char *const *vec)
{
	unsigned n;

	if (!vec)
		return 0;
	for (n = 0; n < IR0_EARLY_ELF_ARG_MAX && vec[n]; n++)
		;
	return n;
}

static int copy_mem(const struct early_user_services *user, uint64_t dest,
		    const void *src, uint64_t size)
{
	if (!user || !user->copy_to_user)
		return -1;
	if (size == 0)
		return 0;
	return user->copy_to_user(dest, src, size);
}

static int copy_u64(const struct early_user_services *user, uint64_t dest,
		    uint64_t value)
{
	return copy_mem(user, dest, &value, sizeof(value));
}

int ir0_early_elf_setup_stack(const struct early_user_services *user,
			      const struct ir0_early_elf_stack_params *params,
			      uint64_t *out_sp)
{
	unsigned argc;
	unsigned envc;
	unsigned i;
	uint64_t argv_ptrs[IR0_EARLY_ELF_ARG_MAX];
	uint64_t envp_ptrs[IR0_EARLY_ELF_ARG_MAX];
	uint64_t strings_size;
	uint64_t stack_size;
	uint64_t stack_base;
	uint64_t argc_slot;
	uint64_t argv_array;
	uint64_t envp_array;
	uint64_t auxv_base;
	uint64_t random_base;
	uint64_t strings_base;
	uint64_t cursor;
	const char *execfn;
	const uint8_t *random;
	uint64_t auxv[EARLY_ELF_AUXV_PAIRS][2];

	if (!user || !params || !out_sp || params->stack_top == 0)
		return -1;

	argc = count_vec(params->argv);
	envc = count_vec(params->envp);
	execfn = params->aux.execfn;
	if (!execfn && argc > 0)
		execfn = params->argv[0];
	if (!execfn)
		execfn = "";
	random = params->aux.random ? params->aux.random : g_default_random;

	strings_size = (uint64_t)cstr_len(execfn) + 1UL;
	for (i = 0; i < argc; i++)
		strings_size += (uint64_t)cstr_len(params->argv[i]) + 1UL;
	for (i = 0; i < envc; i++)
		strings_size += (uint64_t)cstr_len(params->envp[i]) + 1UL;

	stack_size = sizeof(uint64_t) +
		     (argc + 1U) * sizeof(uint64_t) +
		     (envc + 1U) * sizeof(uint64_t) +
		     EARLY_ELF_AUXV_PAIRS * 2U * sizeof(uint64_t) +
		     IR0_EARLY_ELF_AT_RANDOM_BYTES +
		     strings_size + 16UL;
	if (params->stack_size != 0 && stack_size > params->stack_size)
		return -1;

	stack_base = (params->stack_top - stack_size) & ~0xFULL;
	if (stack_base >= params->stack_top)
		return -1;

	argc_slot = stack_base;
	argv_array = argc_slot + sizeof(uint64_t);
	envp_array = argv_array + (argc + 1U) * sizeof(uint64_t);
	auxv_base = envp_array + (envc + 1U) * sizeof(uint64_t);
	random_base = auxv_base + EARLY_ELF_AUXV_PAIRS * 2U * sizeof(uint64_t);
	strings_base = random_base + IR0_EARLY_ELF_AT_RANDOM_BYTES;
	cursor = strings_base;

	for (i = 0; i < argc; i++)
	{
		unsigned len = cstr_len(params->argv[i]) + 1U;

		if (copy_mem(user, cursor, params->argv[i], len) != 0)
			return -1;
		argv_ptrs[i] = cursor;
		cursor += len;
	}
	for (i = 0; i < envc; i++)
	{
		unsigned len = cstr_len(params->envp[i]) + 1U;

		if (copy_mem(user, cursor, params->envp[i], len) != 0)
			return -1;
		envp_ptrs[i] = cursor;
		cursor += len;
	}
	{
		unsigned len = cstr_len(execfn) + 1U;

		if (copy_mem(user, cursor, execfn, len) != 0)
			return -1;
		auxv[13][0] = IR0_AT_EXECFN;
		auxv[13][1] = cursor;
	}

	if (copy_u64(user, argc_slot, argc) != 0)
		return -1;
	for (i = 0; i < argc; i++)
	{
		if (copy_u64(user, argv_array + i * sizeof(uint64_t),
			     argv_ptrs[i]) != 0)
			return -1;
	}
	if (copy_u64(user, argv_array + argc * sizeof(uint64_t), 0) != 0)
		return -1;
	for (i = 0; i < envc; i++)
	{
		if (copy_u64(user, envp_array + i * sizeof(uint64_t),
			     envp_ptrs[i]) != 0)
			return -1;
	}
	if (copy_u64(user, envp_array + envc * sizeof(uint64_t), 0) != 0)
		return -1;
	if (copy_mem(user, random_base, random,
		     IR0_EARLY_ELF_AT_RANDOM_BYTES) != 0)
		return -1;

	auxv[0][0] = IR0_AT_PHDR;
	auxv[0][1] = params->aux.phdr;
	auxv[1][0] = IR0_AT_PHENT;
	auxv[1][1] = params->aux.phent;
	auxv[2][0] = IR0_AT_PHNUM;
	auxv[2][1] = params->aux.phnum;
	auxv[3][0] = IR0_AT_PAGESZ;
	auxv[3][1] = params->aux.pagesz ? params->aux.pagesz : 4096UL;
	auxv[4][0] = IR0_AT_BASE;
	auxv[4][1] = params->aux.base;
	auxv[5][0] = IR0_AT_FLAGS;
	auxv[5][1] = params->aux.flags;
	auxv[6][0] = IR0_AT_ENTRY;
	auxv[6][1] = params->aux.entry;
	auxv[7][0] = IR0_AT_UID;
	auxv[7][1] = params->aux.uid;
	auxv[8][0] = IR0_AT_EUID;
	auxv[8][1] = params->aux.euid;
	auxv[9][0] = IR0_AT_GID;
	auxv[9][1] = params->aux.gid;
	auxv[10][0] = IR0_AT_EGID;
	auxv[10][1] = params->aux.egid;
	auxv[11][0] = IR0_AT_SECURE;
	auxv[11][1] = params->aux.secure;
	auxv[12][0] = IR0_AT_RANDOM;
	auxv[12][1] = random_base;
	auxv[14][0] = IR0_AT_NULL;
	auxv[14][1] = 0;

	for (i = 0; i < EARLY_ELF_AUXV_PAIRS; i++)
	{
		uint64_t off = (uint64_t)i * 2UL * sizeof(uint64_t);

		if (copy_u64(user, auxv_base + off, auxv[i][0]) != 0 ||
		    copy_u64(user, auxv_base + off + sizeof(uint64_t),
			     auxv[i][1]) != 0)
			return -1;
	}

	*out_sp = argc_slot;
	return 0;
}
