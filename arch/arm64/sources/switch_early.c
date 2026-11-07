/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_early.c
 * Description: EL1 kernel-context switch with an optional TTBR0 change.
 */

#include "switch_early.h"

#include <arch/common/arch_portable.h>
#include <stdint.h>

void arm64_cpu_switch_mm(struct arm64_cpu_ctx *prev, struct arm64_cpu_ctx *next,
			 uint64_t next_ttbr)
{
	if (next_ttbr)
		mm_activate((uintptr_t)next_ttbr);
	arm64_cpu_switch(prev, next);
}
