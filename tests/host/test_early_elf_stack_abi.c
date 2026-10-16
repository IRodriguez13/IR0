/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_early_elf_stack_abi.c
 * Description: Host checks for staged SysV argc/argv/auxv layout
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/early_elf_stack.h>
#include <string.h>

#define STACK_BYTES 1024

static unsigned char g_stack[STACK_BYTES];

static int host_copy_from_user(void *destination, uint64_t source, uint64_t size)
{
	if (!destination || source + size > STACK_BYTES)
		return -1;
	memcpy(destination, g_stack + source, (size_t)size);
	return 0;
}

static int host_copy_to_user(uint64_t destination, const void *source,
			     uint64_t size)
{
	if (!source || destination + size > STACK_BYTES)
		return -1;
	memcpy(g_stack + destination, source, (size_t)size);
	return 0;
}

static const struct early_user_services g_user = {
	.copy_from_user = host_copy_from_user,
	.copy_to_user = host_copy_to_user,
};

static uint64_t read_u64(uint64_t va)
{
	uint64_t value;

	memcpy(&value, g_stack + va, sizeof(value));
	return value;
}

void test_early_elf_stack_abi(void)
{
	char *argv[] = { "echo", "ARM64_EL0_OK", 0 };
	struct ir0_early_elf_stack_params params;
	uint64_t sp;
	uint64_t argv0;
	uint64_t argv1;
	uint64_t aux;
	int saw_phdr = 0;
	int saw_entry = 0;
	int saw_execfn = 0;
	int saw_null = 0;
	unsigned i;

	TEST_BEGIN("early_elf_stack_abi");

	memset(g_stack, 0xcc, sizeof(g_stack));
	memset(&params, 0, sizeof(params));
	params.stack_top = STACK_BYTES;
	params.stack_size = STACK_BYTES;
	params.argv = argv;
	params.envp = 0;
	params.aux.phdr = 0x40000040;
	params.aux.phent = 56;
	params.aux.phnum = 4;
	params.aux.pagesz = 4096;
	params.aux.entry = 0x40001000;
	params.aux.execfn = "echo";

	ASSERT_EQ(ir0_early_elf_setup_stack(&g_user, &params, &sp), 0);
	ASSERT_EQ(sp % 16, 0);
	ASSERT_EQ(read_u64(sp), 2);

	argv0 = read_u64(sp + 8);
	argv1 = read_u64(sp + 16);
	ASSERT_EQ(read_u64(sp + 24), 0);
	ASSERT_EQ(read_u64(sp + 32), 0);
	ASSERT_EQ(strcmp((const char *)(g_stack + argv0), "echo"), 0);
	ASSERT_EQ(strcmp((const char *)(g_stack + argv1), "ARM64_EL0_OK"),
		  0);

	aux = sp + 40;
	for (i = 0; i < 16; i++)
	{
		uint64_t type = read_u64(aux + (uint64_t)i * 16);
		uint64_t value = read_u64(aux + (uint64_t)i * 16 + 8);

		if (type == IR0_AT_NULL)
		{
			saw_null = 1;
			break;
		}
		if (type == IR0_AT_PHDR)
		{
			ASSERT_EQ(value, 0x40000040);
			saw_phdr = 1;
		}
		if (type == IR0_AT_ENTRY)
		{
			ASSERT_EQ(value, 0x40001000);
			saw_entry = 1;
		}
		if (type == IR0_AT_EXECFN)
		{
			ASSERT_EQ(strcmp((const char *)(g_stack + value), "echo"),
				  0);
			saw_execfn = 1;
		}
	}

	ASSERT_EQ(saw_phdr, 1);
	ASSERT_EQ(saw_entry, 1);
	ASSERT_EQ(saw_execfn, 1);
	ASSERT_EQ(saw_null, 1);

	TEST_END();
}
