/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_loader.h
 * Description: Kernel-tree alias for includes/ir0/elf_loader.h
 */

#pragma once

#include <ir0/elf_loader.h>

static inline int elf_load_and_execute(const char *path)
	__attribute__((deprecated("use kexecve() instead")));

static inline int elf_load_and_execute(const char *path)
{
	return kexecve(path, NULL, NULL);
}
