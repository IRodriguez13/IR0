/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binaries Bluetooth fallback commands
 * Copyright (C) 2026 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_bt_stub.c
 * Description: Link-time fallback command objects used only when Bluetooth
 * command objects are excluded from build.
 */

#include "debug_bins.h"

static int bt_disabled_handler(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	debug_writeln_err("Bluetooth debug commands unavailable in this build");
	return 127;
}

struct debug_command cmd_lsblue = {
	.name = "lsblue",
	.handler = bt_disabled_handler,
	.usage = "lsblue",
	.description = "Unavailable: Bluetooth disabled at build time",
	.flags = NULL
};

struct debug_command cmd_bluestart = {
	.name = "bluestart",
	.handler = bt_disabled_handler,
	.usage = "bluestart",
	.description = "Unavailable: Bluetooth disabled at build time",
	.flags = NULL
};

struct debug_command cmd_blue = {
	.name = "blue",
	.handler = bt_disabled_handler,
	.usage = "blue",
	.description = "Unavailable: Bluetooth disabled at build time",
	.flags = NULL
};
