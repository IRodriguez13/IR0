/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_log.c
 * Description: Portable, banner-first boot log facade.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_log.h>
#include <ir0/cpu_info.h>
#include <ir0/ktm/klog.h>
#include <ir0/version.h>

#include <stddef.h>
#include <string.h>

enum boot_log_level {
	BOOT_LOG_INFO,
	BOOT_LOG_NOTICE,
	BOOT_LOG_WARN,
};

static void boot_event(enum boot_log_level level, const char *component,
			       const char *message)
{
	static const klog_level_t klog_levels[] = {
		KLOG_LEVEL_INFO,
		KLOG_LEVEL_NOTICE,
		KLOG_LEVEL_WARN,
	};

	klog_event(KLOG_EVENT_BOOT_INFO, 0, klog_levels[level], component, message);
}

void ir0_boot_serial_ready(void)
{
	klog_boot_hold(0);
	klog_event(KLOG_EVENT_BOOT_BANNER, 0, KLOG_LEVEL_INFO, "BOOT",
		   "IR0 kernel " IR0_VERSION_STRING);
}

void ir0_boot_info(const char *component, const char *message)
{
	boot_event(BOOT_LOG_INFO, component, message);
}

void ir0_boot_notice(const char *component, const char *message)
{
	boot_event(BOOT_LOG_NOTICE, component, message);
}

void ir0_boot_warn(const char *component, const char *message)
{
	boot_event(BOOT_LOG_WARN, component, message);
}

void ir0_boot_info_hex64(const char *component, const char *label,
			 uint64_t value)
{
	static const char hex[] = "0123456789ABCDEF";
	char message[96];
	size_t off = 0;
	int shift;

	if (!label)
		label = "value";
	while (label[off] && off + 20U < sizeof(message))
	{
		message[off] = label[off];
		off++;
	}
	message[off++] = '=';
	message[off++] = '0';
	message[off++] = 'x';
	for (shift = 60; shift >= 0; shift -= 4)
		message[off++] = hex[(value >> shift) & 0xfU];
	message[off] = '\0';
	ir0_boot_info(component, message);
}

void ir0_boot_arch(const char *message)
{
	ir0_boot_info("ARCH", message);
}

void ir0_boot_smoke(const char *tag)
{
	klog_smoke(tag);
}

void ir0_boot_log_platform_hypervisor(void)
{
	char hv_vendor[16];

	if (hypervisor_present() &&
	    hypervisor_vendor(hv_vendor, sizeof(hv_vendor)) == 0)
	{
		if (strncmp(hv_vendor, "TCGTCGTCGTCG", 12) == 0)
			ir0_boot_info("HYPERVISOR",
				      "detected vendor=TCG (QEMU TCG)");
		else if (strncmp(hv_vendor, "KVMKVMKVM", 9) == 0)
			ir0_boot_info("HYPERVISOR",
				      "detected vendor=KVM");
		else
			ir0_boot_info("HYPERVISOR", hv_vendor);
	}
	else
	{
		ir0_boot_info("HYPERVISOR", "none detected");
		ir0_boot_notice("PLATFORM",
				"Bare-metal execution confirmed!");
	}
}
