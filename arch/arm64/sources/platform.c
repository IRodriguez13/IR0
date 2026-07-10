/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: platform.c
 * Description: ARM64 platform stubs (CPUID N/A; PSCI power later).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>
#include <ir0/platform_ops.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t arch_get_cpu_id(void)
{
	return 0;
}

uint32_t arch_get_cpu_count(void)
{
	return 1;
}

int arch_get_cpu_vendor(char *vendor_buf)
{
	if (!vendor_buf)
		return -1;
	strncpy(vendor_buf, "Unknown", 8);
	return -1;
}

int arch_get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping)
{
	if (family)
		*family = 0;
	if (model)
		*model = 0;
	if (stepping)
		*stepping = 0;
	return -1;
}

int arch_get_cpuid_max_leaf(uint32_t *max_leaf)
{
	if (max_leaf)
		*max_leaf = 0;
	return -1;
}

int arch_get_cpu_brand_string(char *buf, size_t size)
{
	(void)buf;
	(void)size;
	return -1;
}

int arch_get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx)
{
	if (out_edx)
		*out_edx = 0;
	if (out_ecx)
		*out_ecx = 0;
	return -1;
}

uint32_t arch_get_cpu_clflush_size(void)
{
	return 0;
}

static void __attribute__((noreturn)) arm64_halt_loop(void)
{
	arch_disable_interrupts();
	for (;;)
		cpu_wait();
}

static void arm64_halt(void)
{
	arm64_halt_loop();
}

static void arm64_reboot(void)
{
	/* PSCI SYSTEM_RESET not wired yet. */
	arm64_halt_loop();
}

static void arm64_poweroff(void)
{
	/* PSCI SYSTEM_OFF not wired yet. */
	arm64_halt_loop();
}

static const struct ir0_platform_ops arm64_platform_ops = {
	.halt = arm64_halt,
	.reboot = arm64_reboot,
	.poweroff = arm64_poweroff,
};

static const struct ir0_platform_ops *g_platform_ops = &arm64_platform_ops;

const struct ir0_platform_ops *ir0_platform_ops_get(void)
{
	return g_platform_ops;
}

void ir0_platform_ops_set(const struct ir0_platform_ops *ops)
{
	if (ops)
		g_platform_ops = ops;
}

void arch_system_halt(void)
{
	ir0_platform_ops_get()->halt();
	for (;;)
		cpu_wait();
}

void arch_system_reboot(void)
{
	ir0_platform_ops_get()->reboot();
	for (;;)
		cpu_wait();
}

void arch_system_poweroff(void)
{
	ir0_platform_ops_get()->poweroff();
	for (;;)
		cpu_wait();
}
