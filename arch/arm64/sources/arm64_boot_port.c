/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: arm64_boot_port.c
 * Description: ARM64 boot hooks — DTB handoff for kmain, PL011 before klog, optional init seed.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_log.h>
#include <ir0/boot_port.h>
#include <ir0/serial_io.h>
#include <ir0/ktm/klog.h>

#include <config.h>
#include <stdint.h>
#include <string.h>

#if defined(IR0_ARM64_PRODUCT_KERNEL)
#include <ir0/bpivot.h>
#include <ir0/open_flags.h>
#include <ir0/stat.h>
#include <ir0/vfs.h>
#include <rootfs_base.h>
#endif

#if !defined(IR0_FREESTANDING_BOOT)
extern volatile uintptr_t arm64_firmware_fdt;
#endif

#if defined(IR0_ARM64_PRODUCT_KERNEL)
extern const uint8_t hello_aarch64_blob[];
extern const uint8_t hello_aarch64_blob_end[];

static void boot_arm64_seed_embedded_init(void)
{
	struct vfs_file *f;
	const uint8_t *p;
	size_t remain;
	stat_t st;

	ir0_rootfs_prepare_userspace_base();
	if (vfs_stat("/sbin/init", &st) == 0)
		return;
	p = hello_aarch64_blob;
	remain = (size_t)(hello_aarch64_blob_end - hello_aarch64_blob);
	if (remain == 0U)
		return;
	if (vfs_open("/sbin/init",
		     IR0_O_WRONLY | IR0_O_CREAT | IR0_O_TRUNC, 0755, &f) != 0)
	{
		klog_warn("BOOT",
			  "ARM64: could not create /sbin/init from embedded hello");
		return;
	}
	while (remain > 0U)
	{
		size_t chunk = remain > 4096U ? 4096U : remain;
		int w = vfs_write(f, (const char *)p, chunk);

		if (w < 0 || (size_t)w != chunk)
		{
			klog_warn("BOOT",
				  "ARM64: embedded /sbin/init write incomplete");
			(void)vfs_close(f);
			return;
		}
		p += chunk;
		remain -= chunk;
	}
	(void)vfs_close(f);
	klog_info("BOOT",
		  "ARM64: seeded /sbin/init from embedded hello (no block rootfs)");

	if (ir0_bpivot_product_boot(ir0_bpath_vfs_product_ops()) == 0)
		ir0_boot_smoke("ARM64_BPIVOT_COMMIT_OK");
}
#endif

uintptr_t ir0_kmain_normalize_firmware_handle(uintptr_t firmware_handle)
{
#if defined(IR0_FREESTANDING_BOOT)
	return firmware_handle;
#else
	(void)firmware_handle;
	return (uintptr_t)arm64_firmware_fdt;
#endif
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
	boot_arm64_seed_embedded_init();
#endif
}
