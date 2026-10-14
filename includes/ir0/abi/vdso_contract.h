/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vdso_contract.h
 * Description: Per-ISA vDSO user mapping contract (AT_SYSINFO_EHDR).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#include <asm/vdso_contract.h>

static inline int vdso_have(void)
{
	return IR0_ARCH_VDSO_HAVE != 0;
}

static inline uint64_t vdso_user_base(void)
{
	return (uint64_t)IR0_ARCH_VDSO_USER_BASE;
}
