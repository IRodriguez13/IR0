/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_params.c
 * Description: Storage for the typed boot handoff (see ir0/boot_params.h).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_params.h>

static struct ir0_boot_params g_boot_params;

void set_boot_params(const struct ir0_boot_params *params)
{
	if (!params)
	{
		g_boot_params.protocol = IR0_BOOT_PROTOCOL_UNKNOWN;
		g_boot_params.info = 0;
		return;
	}
	g_boot_params = *params;
}

const struct ir0_boot_params *get_boot_params(void)
{
	return &g_boot_params;
}
