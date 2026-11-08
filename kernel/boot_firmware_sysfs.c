/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_firmware_sysfs.c
 * Description: Portable /sys/firmware/ir0 snapshot (neutral fields + arch detail).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_firmware_sysfs.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_params.h>
#include <ir0/sysfs.h>

#include <string.h>

static struct ir0_boot_firmware_sysfs_snapshot g_boot_fw_snap;
static int g_boot_fw_init;

__attribute__((weak)) void ir0_boot_firmware_sysfs_fill_from_arch(
	struct ir0_boot_firmware_sysfs_snapshot *snap)
{
	(void)snap;
}

static const char *boot_protocol_name(enum ir0_boot_protocol protocol)
{
	switch (protocol)
	{
	case IR0_BOOT_PROTOCOL_MULTIBOOT1:
		return "multiboot1";
	case IR0_BOOT_PROTOCOL_FDT:
		return "fdt";
	case IR0_BOOT_PROTOCOL_UNKNOWN:
	default:
		return "unknown";
	}
}

static const char *boot_memory_map_name(enum ir0_boot_memory_map map)
{
	switch (map)
	{
	case IR0_BOOT_MEMORY_MAP_FIRMWARE:
		return "firmware";
	case IR0_BOOT_MEMORY_MAP_FIRMWARE_SIZE:
		return "firmware-size";
	case IR0_BOOT_MEMORY_MAP_BUILTIN:
	default:
		return "builtin";
	}
}

/* Neutral fields: typed boot record plus the normalized boot snapshot. */
static void boot_fw_fill_common(struct ir0_boot_firmware_sysfs_snapshot *snap)
{
	const struct ir0_boot_snapshot *boot = boot_memory_snapshot();
	const struct ir0_boot_framebuffer *fb;

	snprintf(snap->protocol, sizeof(snap->protocol), "%s",
		 boot_protocol_name(get_boot_params()->protocol));
	if (!boot)
		return;

	snprintf(snap->cmdline, sizeof(snap->cmdline), "%s", boot->cmdline);
	snprintf(snap->memory_map, sizeof(snap->memory_map), "%s",
		 boot_memory_map_name(boot->memory_map));

	fb = &boot->framebuffer;
	if (fb->base != 0 && fb->width != 0)
		snprintf(snap->framebuffer, sizeof(snap->framebuffer),
			 "addr=0x%llx size=0x%llx width=%u height=%u pitch=%u bpp=%u type=%u",
			 (unsigned long long)fb->base,
			 (unsigned long long)fb->size, (unsigned)fb->width,
			 (unsigned)fb->height, (unsigned)fb->pitch,
			 (unsigned)fb->bpp, (unsigned)fb->type);
	else if (fb->base != 0 && fb->size != 0)
		snprintf(snap->framebuffer, sizeof(snap->framebuffer),
			 "addr=0x%llx size=0x%llx",
			 (unsigned long long)fb->base,
			 (unsigned long long)fb->size);

	if (boot->acpi_rsdp != 0)
		snprintf(snap->acpi_rsdp, sizeof(snap->acpi_rsdp), "0x%llx",
			 (unsigned long long)boot->acpi_rsdp);
	if (boot->dtb.size != 0)
		snprintf(snap->dtb, sizeof(snap->dtb), "addr=0x%llx size=0x%llx",
			 (unsigned long long)boot->dtb.base,
			 (unsigned long long)boot->dtb.size);
}

void ir0_boot_firmware_sysfs_init(void)
{
	memset(&g_boot_fw_snap, 0, sizeof(g_boot_fw_snap));
	boot_fw_fill_common(&g_boot_fw_snap);
	ir0_boot_firmware_sysfs_fill_from_arch(&g_boot_fw_snap);
	g_boot_fw_init = 1;
}

static const struct ir0_boot_firmware_sysfs_snapshot *boot_fw_snap_get(void)
{
	if (!g_boot_fw_init)
		ir0_boot_firmware_sysfs_init();
	return &g_boot_fw_snap;
}

/* One newline-terminated line: @value, or @fallback when it is empty. */
static int boot_fw_read_line(char *buf, size_t count, const char *value,
			     const char *fallback)
{
	int len;

	if (!buf || count == 0)
		return -1;
	memset(buf, 0, count);
	len = snprintf(buf, count, "%s\n", value[0] ? value : fallback);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_firmware_ir0_protocol_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->protocol,
				 "unknown");
}

int sys_firmware_ir0_bootloader_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->bootloader,
				 "unavailable");
}

int sys_firmware_ir0_cmdline_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->cmdline, "");
}

int sys_firmware_ir0_memory_map_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->memory_map,
				 "unavailable");
}

int sys_firmware_ir0_framebuffer_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->framebuffer,
				 "unavailable");
}

int sys_firmware_ir0_acpi_rsdp_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->acpi_rsdp,
				 "unavailable");
}

int sys_firmware_ir0_dtb_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->dtb,
				 "unavailable");
}

int sys_firmware_ir0_boot_detail_read_reg(char *buf, size_t count)
{
	return boot_fw_read_line(buf, count, boot_fw_snap_get()->boot_detail,
				 "unavailable");
}
