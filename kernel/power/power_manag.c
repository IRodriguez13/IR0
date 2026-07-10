/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: power_manag.c
 * Description: Coordinated system halt/reboot/poweroff.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "power_manag.h"

#include <ir0/arch_port.h>
#include <ir0/blockdev.h>
#include <ir0/driver.h>
#include <ir0/serial_io.h>

void kernel_system_shutdown(enum ir0_system_action action)
{
	switch (action)
	{
	case IR0_SYSTEM_REBOOT:
		serial_print("SYSTEM_SHUTDOWN_REBOOT\n");
		break;
	case IR0_SYSTEM_POWEROFF:
		serial_print("SYSTEM_SHUTDOWN_POWEROFF\n");
		break;
	case IR0_SYSTEM_HALT:
	default:
		serial_print("SYSTEM_SHUTDOWN_HALT\n");
		action = IR0_SYSTEM_HALT;
		break;
	}

	/* Best-effort sync: no full vfs_sync yet; flush registered block devices. */
	ir0_block_flush_all();
	ir0_driver_shutdown_all();

	arch_disable_interrupts();

	switch (action)
	{
	case IR0_SYSTEM_REBOOT:
		arch_system_reboot();
		break;
	case IR0_SYSTEM_POWEROFF:
		arch_system_poweroff();
		break;
	case IR0_SYSTEM_HALT:
	default:
		arch_system_halt();
		break;
	}

	/* Unreachable if arch callbacks are correct. */
	for (;;)
		arch_cpu_halt();
}
