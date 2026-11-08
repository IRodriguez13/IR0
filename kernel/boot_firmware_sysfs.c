/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_firmware_sysfs.c
 * Description: Portable /sys/firmware/ir0 snapshot (filled by arch backends).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_firmware_sysfs.h>

#include <string.h>

static struct ir0_boot_firmware_sysfs_snapshot g_boot_fw_snap;
static int g_boot_fw_init;

__attribute__((weak)) void ir0_boot_firmware_sysfs_fill_from_arch(
	struct ir0_boot_firmware_sysfs_snapshot *snap)
{
	(void)snap;
}

void ir0_boot_firmware_sysfs_init(void)
{
	memset(&g_boot_fw_snap, 0, sizeof(g_boot_fw_snap));
	snprintf(g_boot_fw_snap.protocol, sizeof(g_boot_fw_snap.protocol), "unknown");
	g_boot_fw_snap.multiboot_valid = 0;
	g_boot_fw_snap.multiboot_flags = 0U;
	snprintf(g_boot_fw_snap.boot_detail, sizeof(g_boot_fw_snap.boot_detail),
		 "unavailable");
	ir0_boot_firmware_sysfs_fill_from_arch(&g_boot_fw_snap);
	g_boot_fw_init = 1;
}

static const struct ir0_boot_firmware_sysfs_snapshot *boot_fw_snap_get(void)
{
	if (!g_boot_fw_init)
		ir0_boot_firmware_sysfs_init();
	return &g_boot_fw_snap;
}

int sys_firmware_ir0_protocol_read_reg(char *buf, size_t count)
{
	const struct ir0_boot_firmware_sysfs_snapshot *s;
	int len;

	if (!buf || count == 0)
		return -1;
	memset(buf, 0, count);
	s = boot_fw_snap_get();
	len = snprintf(buf, count, "%s\n", s->protocol[0] ? s->protocol : "unknown");
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_firmware_ir0_bootloader_read_reg(char *buf, size_t count)
{
	const struct ir0_boot_firmware_sysfs_snapshot *s;
	int len;

	if (!buf || count == 0)
		return -1;
	memset(buf, 0, count);
	s = boot_fw_snap_get();
	len = snprintf(buf, count, "%s\n", s->bootloader[0] ? s->bootloader : "unavailable");
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_firmware_ir0_cmdline_read_reg(char *buf, size_t count)
{
	const struct ir0_boot_firmware_sysfs_snapshot *s;
	int len;

	if (!buf || count == 0)
		return -1;
	memset(buf, 0, count);
	s = boot_fw_snap_get();
	if (!s->cmdline[0])
	{
		buf[0] = '\n';
		return 1;
	}
	len = snprintf(buf, count, "%s\n", s->cmdline);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_firmware_ir0_framebuffer_read_reg(char *buf, size_t count)
{
	const struct ir0_boot_firmware_sysfs_snapshot *s;
	int len;

	if (!buf || count == 0)
		return -1;
	memset(buf, 0, count);
	s = boot_fw_snap_get();
	len = snprintf(buf, count, "%s\n",
		       s->framebuffer[0] ? s->framebuffer : "unavailable");
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}

int sys_firmware_ir0_boot_detail_read_reg(char *buf, size_t count)
{
	const struct ir0_boot_firmware_sysfs_snapshot *s;
	int len;

	if (!buf || count == 0)
		return -1;
	memset(buf, 0, count);
	s = boot_fw_snap_get();
	len = snprintf(buf, count, "%s\n",
		       s->boot_detail[0] ? s->boot_detail : "unavailable");
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return len;
}
