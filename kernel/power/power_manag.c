/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: power_manag.c
 * Description: Coordinated system halt/reboot/poweroff via platform_ops.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "power_manag.h"

#include <ir0/arch_port.h>
#include <ir0/platform_ops.h>
#include <ir0/ktm/klog.h>
#include <ir0/vfs.h>

void kernel_system_shutdown(enum ir0_system_action action)
{
	const struct ir0_platform_ops *ops;

	switch (action)
	{
	case IR0_SYSTEM_REBOOT:
		klog_smoke("SYSTEM_SHUTDOWN_REBOOT");
		break;
	case IR0_SYSTEM_POWEROFF:
		klog_smoke("SYSTEM_SHUTDOWN_POWEROFF");
		break;
	case IR0_SYSTEM_HALT:
	default:
		klog_smoke("SYSTEM_SHUTDOWN_HALT");
		action = IR0_SYSTEM_HALT;
		break;
	}

	klog_smoke("SYSTEM_SYNC_BEGIN");
	(void)vfs_sync();
	klog_smoke("SYSTEM_SYNC_OK");

	/*
	 * Reach platform power ops before driver teardown: some .shutdown hooks
	 * (e.g. input) can fault and would skip ACPI/QEMU poweroff entirely.
	 */
	disable_interrupts();

	ops = ir0_platform_ops_get();
	if (!ops)
	{
		for (;;)
			cpu_halt();
	}

	switch (action)
	{
	case IR0_SYSTEM_REBOOT:
		if (ops->reboot)
			ops->reboot();
		break;
	case IR0_SYSTEM_POWEROFF:
		if (ops->poweroff)
			ops->poweroff();
		break;
	case IR0_SYSTEM_HALT:
	default:
		if (ops->halt)
			ops->halt();
		break;
	}

	/* Unreachable if platform callbacks are correct. */
	for (;;)
		cpu_halt();
}
