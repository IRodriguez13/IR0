/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_port.c
 * Description: ARM64 boot hooks — DTB handoff, PL011.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_log.h>
#include <ir0/boot_params.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_port.h>
#include <arm64_board.h>
#include <ir0/errno.h>
#include <ir0/serial_io.h>
#include <ir0/ktm/klog.h>

#include <config.h>
#include <stdint.h>
#include <string.h>

extern volatile uintptr_t arm64_firmware_fdt;

/* The entry stub stashes the firmware DTB pointer (x0) before kmain. */
void ir0_boot_port_params(uintptr_t firmware_handle,
			  struct ir0_boot_params *params)
{
	params->protocol = IR0_BOOT_PROTOCOL_FDT;
	(void)firmware_handle;
	params->info = (uintptr_t)arm64_firmware_fdt;
}

int ir0_boot_port_snapshot_fill(const struct ir0_boot_params *params,
				struct ir0_boot_snapshot *snapshot)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();
	size_t i;

	(void)params;
	if (!snapshot || !boot ||
	    boot->usable_range_count == 0 ||
	    boot->usable_range_count > IR0_BOOT_MEMORY_MAX_BANKS ||
	    boot->reserved_range_count > IR0_BOOT_MEMORY_MAX_RESERVATIONS)
		return -ENODEV;
	for (i = 0; i < boot->usable_range_count; i++)
		snapshot->usable[i] = boot->usable[i];
	snapshot->usable_count = boot->usable_range_count;
	for (i = 0; i < boot->reserved_range_count; i++)
		snapshot->reserved[i] = boot->reserved[i];
	snapshot->reserved_count = boot->reserved_range_count;
	if (boot->fdt_valid && boot->fdt_size != 0)
	{
		snapshot->firmware.base = boot->fdt_pa;
		snapshot->firmware.size = boot->fdt_size;
		snapshot->dtb = snapshot->firmware;
	}
	if (boot->initrd_start != 0 && boot->initrd_end > boot->initrd_start)
	{
		snapshot->initrd.base = boot->initrd_start;
		snapshot->initrd.size = boot->initrd_end - boot->initrd_start;
	}
	/*
	 * DTB simple-framebuffer. Without a complete mode (width, height,
	 * stride and a known format) only the address range is handed on and
	 * width stays 0, so the video backend leaves the console on serial.
	 */
	snapshot->framebuffer = boot->framebuffer;
	if (boot->framebuffer.width == 0 || boot->framebuffer.height == 0 ||
	    boot->framebuffer.pitch == 0 || boot->framebuffer.bpp == 0)
	{
		snapshot->framebuffer.width = 0;
		snapshot->framebuffer.height = 0;
		snapshot->framebuffer.pitch = 0;
	}
	for (i = 0; i < IR0_BOOT_CMDLINE_MAX - 1U &&
		    boot->bootargs[i] != '\0'; i++)
		snapshot->cmdline[i] = boot->bootargs[i];
	snapshot->cmdline[i] = '\0';
	snapshot->memory_map = boot->fdt_valid ? IR0_BOOT_MEMORY_MAP_FIRMWARE :
					   IR0_BOOT_MEMORY_MAP_BUILTIN;
	snapshot->valid = 1;
	if (boot->fdt_valid)
		ir0_boot_smoke("ARM64_DTB_USABLE_MEMORY_OK");
	return 0;
}

void ir0_boot_port_serial_before_logging(void)
{
	serial_init();
}

void ir0_boot_port_serial_after_driver_registry(void)
{
}
