/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_port.c
 * Description: ARM64 boot hooks — DTB handoff, PL011, CPIO/vda bind for PID 1.
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

#if defined(IR0_ARM64_PRODUCT_KERNEL)
#include <ir0/bpivot.h>
#include <ir0/copy_user.h>
#include <ir0/early_rootfs.h>
#include <rootfs_base.h>
#endif

#if !defined(IR0_FREESTANDING_BOOT)
extern volatile uintptr_t arm64_firmware_fdt;
#endif

#if defined(IR0_ARM64_PRODUCT_KERNEL)
#define ARM64_DISK_INIT_MAX (128UL * 1024UL)

static uint8_t g_disk_init[ARM64_DISK_INIT_MAX];

static int arm64_product_copy_from_user(void *destination, uint64_t source,
					uint64_t size)
{
	return copy_from_user(destination, (const void *)(uintptr_t)source,
			      (size_t)size);
}

static int arm64_product_copy_to_user(uint64_t destination, const void *source,
				      uint64_t size)
{
	return copy_to_user((void *)(uintptr_t)destination, source,
			    (size_t)size);
}

static const struct early_user_services g_product_usercopy = {
	.copy_from_user = arm64_product_copy_from_user,
	.copy_to_user = arm64_product_copy_to_user,
};

/*
 * Bind virtio-blk CPIO (or ELF init) when present. No embedded hello seed —
 * PID 1 must come from the block image via common kexecve("/sbin/init").
 */
static void boot_arm64_bind_block_rootfs(void)
{
	int ret;

	ret = early_rootfs_try_bind_block("vda", g_disk_init,
					  sizeof(g_disk_init),
					  &g_product_usercopy);
	if (ret == 0)
	{
		ir0_boot_smoke("ARM64_DISK_INIT_OK");
		if (early_rootfs_cpio_ready())
			ir0_boot_smoke("ARM64_CPIO_ROOT_OK");
		if (early_rootfs_ready())
			ir0_boot_smoke("ARM64_ROOTFS_OK");
	}
	ir0_rootfs_prepare_userspace_base();
	if (ir0_bpivot_product_boot(ir0_bpath_vfs_product_ops()) == 0)
		ir0_boot_smoke("ARM64_BPIVOT_COMMIT_OK");
}
#endif

/* The entry stub stashes the firmware DTB pointer (x0) before kmain. */
void ir0_boot_port_params(uintptr_t firmware_handle,
			  struct ir0_boot_params *params)
{
	params->protocol = IR0_BOOT_PROTOCOL_FDT;
#if defined(IR0_FREESTANDING_BOOT)
	params->info = firmware_handle;
#else
	(void)firmware_handle;
	params->info = (uintptr_t)arm64_firmware_fdt;
#endif
}

int ir0_boot_port_snapshot_fill(uintptr_t firmware_handle,
				struct ir0_boot_snapshot *snapshot)
{
	const struct arm64_board_boot_info *boot = arm64_board_boot_info();
	size_t i;

	(void)firmware_handle;
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
	snapshot->synthetic_memory = !boot->fdt_valid;
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

void ir0_boot_port_maybe_seed_init_from_embedded(void)
{
#if defined(IR0_ARM64_PRODUCT_KERNEL)
	/*
	 * Not a seed: bind block rootfs / CPIO when present. Missing /sbin/init
	 * fails honestly in the common handoff (same as x86 without ISD).
	 */
	boot_arm64_bind_block_rootfs();
#else
	(void)0;
#endif
}
