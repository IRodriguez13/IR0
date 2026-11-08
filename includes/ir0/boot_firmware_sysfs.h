/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_firmware_sysfs.h
 * Description: Boot/firmware snapshot for sysfs firmware ir0 nodes (portable contract).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IR0_BOOT_FW_SYSFS_STR   256U
#define IR0_BOOT_FW_SYSFS_LINE  512U
#define IR0_BOOT_FW_SYSFS_DETAIL 1024U

/*
 * Text of each /sys/firmware/ir0 node. Common code fills the ISA-neutral
 * fields from the typed boot record and the boot snapshot; the architecture
 * adds only what its protocol alone knows (bootloader, boot_detail). An
 * empty field reads as "unavailable" (cmdline: an empty line).
 */
struct ir0_boot_firmware_sysfs_snapshot
{
	char protocol[IR0_BOOT_FW_SYSFS_STR];
	char bootloader[IR0_BOOT_FW_SYSFS_STR];
	char cmdline[IR0_BOOT_FW_SYSFS_STR];
	char memory_map[IR0_BOOT_FW_SYSFS_STR];
	char framebuffer[IR0_BOOT_FW_SYSFS_LINE];
	char acpi_rsdp[IR0_BOOT_FW_SYSFS_STR];
	char dtb[IR0_BOOT_FW_SYSFS_STR];
	char boot_detail[IR0_BOOT_FW_SYSFS_DETAIL];
};

/*
 * Architecture adds the protocol-specific fields (bootloader name,
 * boot_detail lines). Default weak implementation leaves them empty.
 */
void ir0_boot_firmware_sysfs_fill_from_arch(struct ir0_boot_firmware_sysfs_snapshot *snap);

/* Called once during boot_early after arch firmware setup and the boot snapshot. */
void ir0_boot_firmware_sysfs_init(void);

#ifdef __cplusplus
}
#endif
