/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_firmware_sysfs_arch.c
 * Description: x86_64 Multiboot handoff snapshot for sysfs firmware ir0 nodes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_firmware_sysfs.h>
#include <ir0/boot_memory.h>
#include <ir0/multiboot.h>

#include <string.h>

/*
 * Identity-mapped low RAM: bounded copy from Multiboot physical pointers.
 * Valid only while those regions remain mapped (early boot before the snapshot).
 */
static size_t copy_phys_cstr_trunc(uint32_t phys, char *dst, size_t dst_sz,
				   size_t max_read)
{
	size_t i;

	if (!dst || dst_sz == 0)
		return 0;
	dst[0] = '\0';
	if (phys == 0 || phys >= 0x20000000U ||
	    max_read > 0x20000000U - phys)
		return 0;
	for (i = 0; i + 1 < dst_sz && i < max_read; i++)
	{
		unsigned char c;

		c = *(const volatile unsigned char *)((uintptr_t)phys + i);
		if (c == 0)
			break;
		dst[i] = (char)c;
	}
	dst[i] = '\0';
	return i;
}

void ir0_boot_firmware_sysfs_fill_from_arch(struct ir0_boot_firmware_sysfs_snapshot *snap)
{
	const struct multiboot_info *mb;
	const struct ir0_boot_snapshot *snapshot;
	uint32_t addr_lo;

	if (!snap)
		return;

	snapshot = boot_memory_snapshot();
	mb = (const struct multiboot_info *)get_boot_params();
	if (!mb || !snapshot)
	{
		snprintf(snap->protocol, sizeof(snap->protocol), "multiboot1");
		snprintf(snap->boot_detail, sizeof(snap->boot_detail),
			 "multiboot_valid=0\nreason=no_info_pointer\n");
		return;
	}

	snap->multiboot_valid = 1;
	snap->multiboot_flags = (uint64_t)mb->flags;
	snprintf(snap->protocol, sizeof(snap->protocol), "multiboot1");

	if ((mb->flags & MULTIBOOT_FLAG_LOADER_NAME) && mb->boot_loader_name)
		copy_phys_cstr_trunc(mb->boot_loader_name, snap->bootloader,
				    sizeof(snap->bootloader), 255);
	else
		snprintf(snap->bootloader, sizeof(snap->bootloader), "unavailable");

	snprintf(snap->cmdline, sizeof(snap->cmdline), "%s",
		 snapshot->cmdline);

	if (mb->flags & MULTIBOOT_FLAG_FB)
	{
		uint64_t fb_pa;

		fb_pa = mb->framebuffer_addr;
		addr_lo = (uint32_t)(fb_pa & 0xFFFFFFFFu);
		(void)addr_lo;
		snprintf(snap->framebuffer, sizeof(snap->framebuffer),
			 "addr=0x%llx pitch=%u width=%u height=%u bpp=%u type=%u",
			 (unsigned long long)fb_pa, (unsigned)mb->framebuffer_pitch,
			 (unsigned)mb->framebuffer_width,
			 (unsigned)mb->framebuffer_height,
			 (unsigned)mb->framebuffer_bpp,
			 (unsigned)mb->framebuffer_type);
	}
	else
	{
		snprintf(snap->framebuffer, sizeof(snap->framebuffer), "unavailable");
	}

	snprintf(snap->boot_detail, sizeof(snap->boot_detail),
		 "multiboot_valid=1\n"
		 "multiboot_flags=0x%08x\n"
		 "mem_lower_kib=%u mem_upper_kib=%u\n"
		 "boot_device=0x%08x modules=%u\n",
		 (unsigned)mb->flags,
		 (unsigned)mb->mem_lower, (unsigned)mb->mem_upper,
		 (unsigned)mb->boot_device, (unsigned)mb->mods_count);
}
