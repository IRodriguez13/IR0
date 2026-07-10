/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: power_manag.h
 * Description: System halt/reboot/poweroff coordinator facade (impl in kernel/power/).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Linux uapi/linux/reboot.h magic and commands (reboot(2) ABI).
 * Source: https://man7.org/linux/man-pages/man2/reboot.2.html
 */
#define LINUX_REBOOT_MAGIC1		0xfee1dead
#define LINUX_REBOOT_MAGIC2		672274793
#define LINUX_REBOOT_MAGIC2A		85072278
#define LINUX_REBOOT_MAGIC2B		369367448
#define LINUX_REBOOT_MAGIC2C		537993216

#define LINUX_REBOOT_CMD_RESTART	0x01234567u
#define LINUX_REBOOT_CMD_HALT		0xCDEF0123u
#define LINUX_REBOOT_CMD_CAD_ON		0x89ABCDEFu
#define LINUX_REBOOT_CMD_CAD_OFF	0x00000000u
#define LINUX_REBOOT_CMD_POWER_OFF	0x4321FEDCu
#define LINUX_REBOOT_CMD_RESTART2	0xA1B2C3D4u
#define LINUX_REBOOT_CMD_SW_SUSPEND	0xD000FCE2u
#define LINUX_REBOOT_CMD_KEXEC		0x45584543u

enum ir0_system_action
{
	IR0_SYSTEM_HALT = 0,
	IR0_SYSTEM_REBOOT,
	IR0_SYSTEM_POWEROFF,
};

/**
 * Coordinated shutdown: best-effort block flush, driver .shutdown,
 * disable IRQs, then arch_system_* (does not return).
 */
void kernel_system_shutdown(enum ir0_system_action action)
	__attribute__((noreturn));
