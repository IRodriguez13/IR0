/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: host_sysfs_attr_stub.c
 * Description: Host stand-ins for the /sys attribute readers and writers
 *              the sysfs vfs_fstype calls. Two CPUs. Hostname is mutable.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fs/sysfs_internal.h>
#include <ir0/errno.h>
#include <ir0/power_manag.h>
#include <string.h>

static char host_hostname[64] = "unix";
static uint32_t host_sys_max_processes = 1024;

uint32_t get_cpu_count(void)
{
	return 2;
}

uint32_t sys_kernel_max_processes_limit(void)
{
	return host_sys_max_processes;
}

int sys_kernel_hostname_read_reg(char *buf, size_t count)
{
	int len;

	if (!buf || count == 0)
		return -EINVAL;
	len = snprintf(buf, count, "%s\n", host_hostname);
	if (len < 0)
		return -EIO;
	if (len >= (int)count)
		return (int)(count - 1);
	return len;
}

int sys_kernel_hostname_write_reg(const char *buf, size_t count)
{
	size_t n;

	if (!buf || count == 0)
		return 0;
	n = count < sizeof(host_hostname) - 1 ? count : sizeof(host_hostname) - 1;
	memcpy(host_hostname, buf, n);
	host_hostname[n] = '\0';
	while (n > 0 && (host_hostname[n - 1] == '\n' ||
			 host_hostname[n - 1] == '\r' ||
			 host_hostname[n - 1] == ' ' ||
			 host_hostname[n - 1] == '\t'))
	{
		n--;
		host_hostname[n] = '\0';
	}
	if (n == 0)
		return -EINVAL;
	return (int)count;
}

int sys_kernel_version_read_reg(char *buf, size_t count)
{
	int len;

	if (!buf || count == 0)
		return -EINVAL;
	len = snprintf(buf, count, "ir0-test\n");
	if (len < 0 || len >= (int)count)
		return -EIO;
	return len;
}

int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count)
{
	(void)cpu;
	(void)buf;
	(void)count;
	return -EOPNOTSUPP;
}

int sys_kernel_max_processes_write_reg(const char *buf, size_t count)
{
	(void)buf;
	(void)count;
	return -EINVAL;
}

int sys_kernel_panic_write_reg(const char *buf, size_t count)
{
	(void)buf;
	(void)count;
	return 0;
}

void kernel_system_shutdown(enum ir0_system_action action)
{
	(void)action;
	for (;;)
		;
}

#define SYSFS_EMPTY_READ(fn)                                   \
	int fn(char *buf, size_t count)                        \
	{                                                      \
		if (!buf || count == 0)                        \
			return -EINVAL;                       \
		buf[0] = '\0';                                 \
		return 0;                                      \
	}

SYSFS_EMPTY_READ(sys_kernel_osrelease_read_reg)
SYSFS_EMPTY_READ(sys_kernel_build_read_reg)
SYSFS_EMPTY_READ(sys_kernel_features_read_reg)
SYSFS_EMPTY_READ(sys_kernel_mm_read_reg)
SYSFS_EMPTY_READ(sys_kernel_max_processes_read_reg)
SYSFS_EMPTY_READ(sys_kernel_panic_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_protocol_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_bootloader_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_cmdline_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_memory_map_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_framebuffer_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_acpi_rsdp_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_dtb_read_reg)
SYSFS_EMPTY_READ(sys_firmware_ir0_boot_detail_read_reg)
SYSFS_EMPTY_READ(sys_devices_system_read_reg)
SYSFS_EMPTY_READ(sys_devices_block_read_reg)
SYSFS_EMPTY_READ(sys_console_mode_read_reg)
SYSFS_EMPTY_READ(sys_class_net_list_read_reg)
SYSFS_EMPTY_READ(sys_devices_cpus_read_reg)

int sys_devices_cpu_read_reg(char *buf, size_t count, unsigned cpu)
{
	(void)cpu;
	if (!buf || count == 0)
		return -EINVAL;
	buf[0] = '\0';
	return 0;
}

int sys_devices_cpu_online_read_reg(char *buf, size_t count, unsigned cpu)
{
	int len;

	if (!buf || count == 0)
		return -EINVAL;
	len = snprintf(buf, count, "%u\n", cpu);
	if (len < 0 || len >= (int)count)
		return -EIO;
	return len;
}
