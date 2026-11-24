/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: utsname.c
 * Description: Runtime uname(2) version/nodename from live kernel state.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/utsname.h>
#include <ir0/sched.h>
#include <ir0/sysfs.h>
#include <config.h>
#include <string.h>

#ifndef CONFIG_ENABLE_SMP
#define CONFIG_ENABLE_SMP 0
#endif

/*
 * Absolute value supplied by the linker for every concrete kernel image.
 * It is read through a data word: a PC-relative reference cannot reach a
 * small absolute value from a kernel linked in the upper half.
 */
extern char ir0_build_number[];
static char *const volatile ir0_build_number_ref = ir0_build_number;

void ir0_utsname_fill_version(char *dst, size_t n)
{
	const char *cpu_mode;
	const char *sched_name;
	const char *sched_label;
	int online;

	if (!dst || n == 0)
		return;

	online = sys_devices_cpu_online_count();
	/*
	 * Honest runtime: SMP only when the kernel was built with SMP hooks
	 * and more than one CPU is marked online. Topology alone is not enough.
	 */
	if (CONFIG_ENABLE_SMP && online > 1)
		cpu_mode = "SMP";
	else
		cpu_mode = "UP";

	/*
	 * Label the scheduler that is actually linked. Unknown names pass
	 * through; only a missing name stays RR.
	 */
	sched_name = sched_policy_name();
	if (!sched_name)
		sched_label = "RR";
	else if (strcmp(sched_name, "priority") == 0)
		sched_label = "Priority";
	else if (strcmp(sched_name, "round_robin") == 0)
		sched_label = "RR";
	else if (strcmp(sched_name, "cfs") == 0)
		sched_label = "CFS";
	else
		sched_label = sched_name;

	/* Linux exposes the build identity in the uname version field. */
	snprintf(dst, n, "#%lu %s %s",
		 (unsigned long)(uintptr_t)ir0_build_number_ref,
		 cpu_mode, sched_label);
	dst[n - 1] = '\0';
}

void ir0_utsname_fill_nodename(char *dst, size_t n)
{
	char host[64];
	int len;
	size_t i;

	if (!dst || n == 0)
		return;

	memset(dst, 0, n);
	memset(host, 0, sizeof(host));
	len = sys_kernel_hostname_read_reg(host, sizeof(host));
	if (len <= 0)
	{
		strncpy(dst, "unix", n - 1);
		dst[n - 1] = '\0';
		return;
	}

	for (i = 0; i < (size_t)len && i + 1 < n; i++)
	{
		if (host[i] == '\n' || host[i] == '\r' || host[i] == '\0')
			break;
		dst[i] = host[i];
	}
	dst[i] = '\0';
	if (dst[0] == '\0')
	{
		strncpy(dst, "unix", n - 1);
		dst[n - 1] = '\0';
	}
}
