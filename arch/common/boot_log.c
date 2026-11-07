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
#include <ir0/serial_io.h>
#include <ir0/version.h>

#include <stddef.h>
#include <string.h>

#ifndef IR0_FREESTANDING_BOOT
#include <ir0/ktm/klog.h>
#endif

#ifdef IR0_FREESTANDING_BOOT
/* Hold serial until banner — matches klog_boot_hold default. */
static int g_boot_hold = 1;
static uint64_t g_boot_sequence;

static void boot_append(char *buf, size_t size, size_t *offset, const char *text)
{
	size_t i;

	if (!buf || !offset || !text || size == 0)
		return;
	for (i = 0; text[i] && *offset + 1 < size; i++)
		buf[(*offset)++] = text[i];
	buf[*offset] = '\0';
}

static void boot_emit_raw(const char *level, const char *comp, const char *msg)
{
	char line[384];
	char digits[24];
	unsigned digit_count = 0;
	size_t off = 0;
	uint64_t sequence;

	if (g_boot_hold)
		return;
	if (!level)
		level = "INFO";
	if (!comp)
		comp = "?";
	if (!msg)
		msg = "";

	sequence = ++g_boot_sequence;
	do
	{
		digits[digit_count++] = (char)('0' + (sequence % 10));
		sequence /= 10;
	} while (sequence && digit_count < sizeof(digits));
	boot_append(line, sizeof(line), &off, "[#");
	while (digit_count > 0 && off + 1 < sizeof(line))
		line[off++] = digits[--digit_count];
	line[off] = '\0';
	boot_append(line, sizeof(line), &off,
		    "] [EARLY_ARCH] [    ?.???] [");
	boot_append(line, sizeof(line), &off, level);
	boot_append(line, sizeof(line), &off, "] [");
	boot_append(line, sizeof(line), &off, comp);
	boot_append(line, sizeof(line), &off, "] ");
	boot_append(line, sizeof(line), &off, msg);
	boot_append(line, sizeof(line), &off, "\n");
	serial_print(line);
}
#endif

enum boot_log_level {
	BOOT_LOG_INFO,
	BOOT_LOG_NOTICE,
	BOOT_LOG_WARN,
};

static void boot_event(enum boot_log_level level, const char *component,
			       const char *message)
{
#ifndef IR0_FREESTANDING_BOOT
	static const klog_level_t klog_levels[] = {
		KLOG_LEVEL_INFO,
		KLOG_LEVEL_NOTICE,
		KLOG_LEVEL_WARN,
	};

	klog_event(KLOG_EVENT_BOOT_INFO, 0, klog_levels[level], component, message);
#else
	static const char *const level_names[] = {
		"INFO",
		"NOTICE",
		"WARN",
	};

	boot_emit_raw(level_names[level], component, message);
#endif
}

void ir0_boot_serial_ready(void)
{
#ifndef IR0_FREESTANDING_BOOT
	klog_boot_hold(0);
	klog_event(KLOG_EVENT_BOOT_BANNER, 0, KLOG_LEVEL_INFO, "BOOT",
		   "IR0 kernel " IR0_VERSION_STRING);
#else
	g_boot_hold = 0;
	boot_emit_raw("INFO", "BOOT", "IR0 kernel " IR0_VERSION_STRING);
#endif
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
#ifndef IR0_FREESTANDING_BOOT
	klog_smoke(tag);
#else
	boot_emit_raw("INFO", "SMOKE", tag ? tag : "?");
#endif
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
