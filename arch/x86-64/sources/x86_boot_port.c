/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: x86_boot_port.c
 * Description: x86_64 boot hooks — serial after driver registry; kmain handle passthrough.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_port.h>
#include <ir0/boot_memory.h>
#include <ir0/errno.h>
#include <ir0/multiboot.h>
#include <ir0/pmm.h>
#include <ir0/serial_io.h>
#include <ir0/video_backend.h>

#include <config.h>
#include <stddef.h>
#include <stdint.h>

#define X86_BOOT_IDENTITY_LIMIT 0x20000000ULL
#define X86_MULTIBOOT_MMAP_MAX  (1024U * 1024U)
#define X86_MULTIBOOT_MODULE_MAX 32U

struct x86_multiboot_mmap_entry
{
	uint32_t size;
	uint64_t address;
	uint64_t length;
	uint32_t type;
} __attribute__((packed));

struct x86_multiboot_module
{
	uint32_t start;
	uint32_t end;
	uint32_t string;
	uint32_t reserved;
};

extern char _end[];

static int x86_boot_pointer_range(uint64_t address, uint64_t size)
{
	return address != 0 && size != 0 &&
	       address < X86_BOOT_IDENTITY_LIMIT &&
	       size <= X86_BOOT_IDENTITY_LIMIT - address;
}

static int x86_snapshot_add_range(struct ir0_phys_range *ranges,
				  size_t *count, size_t capacity,
				  uint64_t base, uint64_t size)
{
	if (size == 0 || base > UINT64_MAX - size || *count >= capacity)
		return -EINVAL;
	ranges[*count].base = base;
	ranges[*count].size = size;
	(*count)++;
	return 0;
}

static int x86_snapshot_add_usable(struct ir0_boot_snapshot *snapshot,
				   uint64_t base, uint64_t size)
{
	uint64_t end;
	uint64_t managed_start = PMM_PHYS_BASE;
	uint64_t managed_end = PMM_PHYS_BASE + PMM_PHYS_SIZE;

	if (size == 0 || base > UINT64_MAX - size)
		return -EINVAL;
	end = base + size;
	if (base < managed_start)
		base = managed_start;
	if (end > managed_end)
		end = managed_end;
	if (end <= base)
		return 0;
	return x86_snapshot_add_range(snapshot->usable,
				      &snapshot->usable_count,
				      IR0_BOOT_MEMORY_MAX_BANKS,
				      base, end - base);
}

static int x86_snapshot_add_reserved(struct ir0_boot_snapshot *snapshot,
				     uint64_t base, uint64_t size)
{
	return x86_snapshot_add_range(snapshot->reserved,
				      &snapshot->reserved_count,
				      IR0_BOOT_MEMORY_MAX_RESERVATIONS,
				      base, size);
}

static int x86_snapshot_copy_cmdline(struct ir0_boot_snapshot *snapshot,
				     uint32_t address)
{
	const char *source;
	size_t i;

	if (!x86_boot_pointer_range(address, IR0_BOOT_CMDLINE_MAX))
		return -EINVAL;
	source = (const char *)(uintptr_t)address;
	for (i = 0; i < IR0_BOOT_CMDLINE_MAX - 1U && source[i] != '\0'; i++)
		snapshot->cmdline[i] = source[i];
	if (source[i] != '\0')
		return -EINVAL;
	snapshot->cmdline[i] = '\0';
	return x86_snapshot_add_reserved(snapshot, address, i + 1U);
}

static int x86_snapshot_parse_mmap(struct ir0_boot_snapshot *snapshot,
				   const struct multiboot_info *info)
{
	uint32_t offset = 0;

	if (info->mmap_length == 0 ||
	    info->mmap_length > X86_MULTIBOOT_MMAP_MAX ||
	    !x86_boot_pointer_range(info->mmap_addr, info->mmap_length))
		return -EINVAL;
	while (offset < info->mmap_length)
	{
		const struct x86_multiboot_mmap_entry *entry;
		uint32_t step;

		if (info->mmap_length - offset < sizeof(uint32_t))
			return -EINVAL;
		entry = (const struct x86_multiboot_mmap_entry *)(uintptr_t)
			(info->mmap_addr + offset);
		if (entry->size < 20U || entry->size > UINT32_MAX - 4U)
			return -EINVAL;
		step = entry->size + 4U;
		if (step > info->mmap_length - offset)
			return -EINVAL;
		if (entry->type == 1U &&
		    x86_snapshot_add_usable(snapshot, entry->address,
					    entry->length) != 0)
			return -EINVAL;
		offset += step;
	}
	return x86_snapshot_add_reserved(snapshot, info->mmap_addr,
					 info->mmap_length);
}

static int x86_snapshot_parse_modules(struct ir0_boot_snapshot *snapshot,
				      const struct multiboot_info *info)
{
	const struct x86_multiboot_module *modules;
	uint64_t array_bytes;
	uint32_t i;

	if (info->mods_count == 0)
		return 0;
	if (info->mods_count > X86_MULTIBOOT_MODULE_MAX)
		return -EINVAL;
	array_bytes = (uint64_t)info->mods_count * sizeof(*modules);
	if (!x86_boot_pointer_range(info->mods_addr, array_bytes) ||
	    x86_snapshot_add_reserved(snapshot, info->mods_addr,
				      array_bytes) != 0)
		return -EINVAL;
	modules = (const struct x86_multiboot_module *)(uintptr_t)info->mods_addr;
	for (i = 0; i < info->mods_count; i++)
	{
		uint64_t size;

		if (modules[i].end <= modules[i].start)
			return -EINVAL;
		size = (uint64_t)modules[i].end - modules[i].start;
		if (x86_snapshot_add_reserved(snapshot, modules[i].start,
					      size) != 0)
			return -EINVAL;
		if (i == 0)
		{
			snapshot->initrd.base = modules[i].start;
			snapshot->initrd.size = size;
		}
	}
	return 0;
}

uintptr_t ir0_kmain_normalize_firmware_handle(uintptr_t firmware_handle)
{
	return firmware_handle;
}

int ir0_boot_port_snapshot_fill(uintptr_t firmware_handle,
				struct ir0_boot_snapshot *snapshot)
{
	const struct multiboot_info *info;
	uint64_t kernel_end = (uint64_t)(uintptr_t)_end;

	if (!snapshot ||
	    !x86_boot_pointer_range(firmware_handle, sizeof(*info)))
		return -EINVAL;
	info = (const struct multiboot_info *)firmware_handle;
	snapshot->firmware.base = firmware_handle;
	snapshot->firmware.size = sizeof(*info);
	if (x86_snapshot_add_reserved(snapshot, firmware_handle,
				      sizeof(*info)) != 0 ||
	    x86_snapshot_add_reserved(snapshot, 0, 0x100000U) != 0 ||
	    kernel_end <= 0x100000U ||
	    x86_snapshot_add_reserved(snapshot, 0x100000U,
				      kernel_end - 0x100000U) != 0 ||
	    x86_snapshot_add_reserved(snapshot, 0x00800000U,
				      0x01800000U) != 0)
		return -EINVAL;

	if (info->flags & MULTIBOOT_FLAG_MMAP)
	{
		if (x86_snapshot_parse_mmap(snapshot, info) != 0)
			return -EINVAL;
	}
	else if ((info->flags & MULTIBOOT_FLAG_MEM) && info->mem_upper != 0)
	{
		if (x86_snapshot_add_usable(snapshot, 0x100000U,
					    (uint64_t)info->mem_upper * 1024U) != 0)
			return -EINVAL;
		snapshot->synthetic_memory = 1;
	}
	else
	{
		if (x86_snapshot_add_usable(snapshot, PMM_PHYS_BASE,
					    PMM_PHYS_SIZE) != 0)
			return -EINVAL;
		snapshot->synthetic_memory = 1;
	}

	if ((info->flags & MULTIBOOT_FLAG_CMDLINE) && info->cmdline != 0 &&
	    x86_snapshot_copy_cmdline(snapshot, info->cmdline) != 0)
		return -EINVAL;
	if ((info->flags & MULTIBOOT_FLAG_MODS) &&
	    x86_snapshot_parse_modules(snapshot, info) != 0)
		return -EINVAL;
	if (info->flags & MULTIBOOT_FLAG_FB)
	{
		uint64_t framebuffer_size =
			(uint64_t)info->framebuffer_pitch *
			(uint64_t)info->framebuffer_height;

		if (framebuffer_size != 0 &&
		    info->framebuffer_addr <= UINT64_MAX - framebuffer_size)
		{
			snapshot->framebuffer.base = info->framebuffer_addr;
			snapshot->framebuffer.size = framebuffer_size;
			if (x86_snapshot_add_reserved(snapshot,
						      info->framebuffer_addr,
						      framebuffer_size) != 0)
				return -EINVAL;
		}
	}
	snapshot->valid = snapshot->usable_count != 0;
	return snapshot->valid ? 0 : -ENODEV;
}

int ir0_boot_port_video_init(uintptr_t firmware_handle)
{
	if (firmware_handle > UINT32_MAX)
		return -EINVAL;
	return video_backend_init_from_multiboot((uint32_t)firmware_handle);
}

void ir0_boot_port_serial_before_logging(void)
{
}

void ir0_boot_port_serial_after_driver_registry(void)
{
	serial_init();
}

void ir0_boot_port_maybe_seed_init_from_embedded(void)
{
}
