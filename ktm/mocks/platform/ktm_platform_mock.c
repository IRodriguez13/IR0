/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_platform_mock.c
 * Description: KTM platform ops mock — records power transitions, never resets.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_mock_platform.h>

static unsigned g_halt_count;
static unsigned g_reboot_count;
static unsigned g_poweroff_count;

static void ktm_platform_mock_halt(void)
{
	g_halt_count++;
}

static void ktm_platform_mock_reboot(void)
{
	g_reboot_count++;
}

static void ktm_platform_mock_poweroff(void)
{
	g_poweroff_count++;
}

const struct ir0_platform_ops ktm_platform_mock_ops = {
	.halt = ktm_platform_mock_halt,
	.reboot = ktm_platform_mock_reboot,
	.poweroff = ktm_platform_mock_poweroff,
};

void ktm_platform_mock_reset(void)
{
	g_halt_count = 0;
	g_reboot_count = 0;
	g_poweroff_count = 0;
}

unsigned ktm_platform_mock_halt_count(void)
{
	return g_halt_count;
}

unsigned ktm_platform_mock_reboot_count(void)
{
	return g_reboot_count;
}

unsigned ktm_platform_mock_poweroff_count(void)
{
	return g_poweroff_count;
}
