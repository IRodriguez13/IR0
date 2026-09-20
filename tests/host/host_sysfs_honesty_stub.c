/**
 * Host stub — sysfs honesty helpers (keep aligned with fs/sysfs.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sysfs.h>
#include <ir0/errno.h>
#include <stdint.h>

static uint32_t host_sys_max_processes = 1024;

uint32_t sys_kernel_max_processes_limit(void)
{
	return host_sys_max_processes;
}

int sys_kernel_process_live_count(void)
{
	return 1;
}

int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count)
{
	(void)cpu;
	(void)buf;
	(void)count;
	return -EOPNOTSUPP;
}
