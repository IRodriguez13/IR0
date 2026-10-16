/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_elf_stack.h
 * Description: Staged SysV argc/argv/envp/auxv (ISA supplies usercopy + VAs)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/early_syscall.h>

#define IR0_EARLY_ELF_ARG_MAX 8U
#define IR0_EARLY_ELF_AT_RANDOM_BYTES 16U

#define IR0_AT_NULL   0U
#define IR0_AT_PHDR   3U
#define IR0_AT_PHENT  4U
#define IR0_AT_PHNUM  5U
#define IR0_AT_PAGESZ 6U
#define IR0_AT_BASE   7U
#define IR0_AT_FLAGS  8U
#define IR0_AT_ENTRY  9U
#define IR0_AT_UID    11U
#define IR0_AT_EUID   12U
#define IR0_AT_GID    13U
#define IR0_AT_EGID   14U
#define IR0_AT_SECURE 23U
#define IR0_AT_RANDOM 25U
#define IR0_AT_EXECFN 31U

struct ir0_early_elf_auxv
{
	uint64_t phdr;
	uint64_t phent;
	uint64_t phnum;
	uint64_t pagesz;
	uint64_t base;
	uint64_t flags;
	uint64_t entry;
	uint64_t uid;
	uint64_t euid;
	uint64_t gid;
	uint64_t egid;
	uint64_t secure;
	const uint8_t *random;
	const char *execfn;
};

struct ir0_early_elf_stack_params
{
	uint64_t stack_top;
	uint64_t stack_size;
	char *const *argv;
	char *const *envp;
	struct ir0_early_elf_auxv aux;
};

int ir0_early_elf_setup_stack(const struct early_user_services *user,
			      const struct ir0_early_elf_stack_params *params,
			      uint64_t *out_sp);
