/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cpu_topology.c
 * Description: Architecture-neutral logical CPU topology for UP builds.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cpu.h>
#include <config.h>

#if defined(CONFIG_SMP) && CONFIG_SMP
#error "cpu_topology: SMP identity backend not implemented"
#endif

#define IR0_BOOT_CPU_BIT UINT64_C(1)

unsigned int cpu_id(void)
{
	return 0;
}

uint64_t cpu_possible_mask(void)
{
	return IR0_BOOT_CPU_BIT;
}

uint64_t cpu_present_mask(void)
{
	return IR0_BOOT_CPU_BIT;
}

uint64_t cpu_online_mask(void)
{
	return IR0_BOOT_CPU_BIT;
}

uint64_t cpu_active_mask(void)
{
	return IR0_BOOT_CPU_BIT;
}

unsigned int cpu_online_count(void)
{
	return 1;
}

static int cpu_mask_has(uint64_t mask, unsigned int cpu)
{
	if (cpu >= 64)
		return 0;
	return (mask & (UINT64_C(1) << cpu)) != 0;
}

int cpu_is_possible(unsigned int cpu)
{
	return cpu_mask_has(cpu_possible_mask(), cpu);
}

int cpu_is_present(unsigned int cpu)
{
	return cpu_mask_has(cpu_present_mask(), cpu);
}

int cpu_is_online(unsigned int cpu)
{
	return cpu_mask_has(cpu_online_mask(), cpu);
}

int cpu_is_active(unsigned int cpu)
{
	return cpu_mask_has(cpu_active_mask(), cpu);
}
