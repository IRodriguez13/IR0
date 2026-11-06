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

struct ir0_boot_firmware_sysfs_snapshot
{
	char protocol[IR0_BOOT_FW_SYSFS_STR];
	char bootloader[IR0_BOOT_FW_SYSFS_STR];
	char cmdline[IR0_BOOT_FW_SYSFS_STR];
	char framebuffer[IR0_BOOT_FW_SYSFS_LINE];
	char boot_detail[IR0_BOOT_FW_SYSFS_DETAIL];
	uint64_t multiboot_flags;
	int multiboot_valid;
};

/*
 * Architecture fills @snap from firmware handoff (Multiboot pointer, DTB parse, …).
 * Default weak implementation leaves portable defaults ("unknown", empty strings).
 */
void ir0_boot_firmware_sysfs_fill_from_arch(struct ir0_boot_firmware_sysfs_snapshot *snap);

/* Called once during boot_early after arch firmware setup and set_boot_params(). */
void ir0_boot_firmware_sysfs_init(void);

#ifdef __cplusplus
}
#endif
