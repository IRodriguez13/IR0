/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_log.c
 * Description: Portable boot serial contract (banner-first, ISA-agnostic format).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_log.h>
#include <ir0/serial_io.h>
#include <ir0/version.h>

#include <stddef.h>

#ifndef IR0_FREESTANDING_BOOT
#include <ir0/ktm/klog.h>
#endif

#ifdef IR0_FREESTANDING_BOOT
/* Hold serial until banner — matches klog_boot_hold default. */
static int g_boot_hold = 1;
static uint64_t g_boot_sequence;

static void boot_append(char *buf, size_t sz, size_t *off, const char *s)
{
	size_t i;

	if (!buf || !off || !s || sz == 0)
		return;
	for (i = 0; s[i] && *off + 1 < sz; i++)
		buf[(*off)++] = s[i];
	buf[*off] = '\0';
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
#ifndef IR0_FREESTANDING_BOOT
	klog_event(KLOG_EVENT_BOOT_INFO, 0, KLOG_LEVEL_INFO, component, message);
#else
	boot_emit_raw("INFO", component, message);
#endif
}

void ir0_boot_notice(const char *component, const char *message)
{
#ifndef IR0_FREESTANDING_BOOT
	klog_event(KLOG_EVENT_BOOT_INFO, 0, KLOG_LEVEL_NOTICE, component,
		   message);
#else
	boot_emit_raw("NOTICE", component, message);
#endif
}

void ir0_boot_warn(const char *component, const char *message)
{
#ifndef IR0_FREESTANDING_BOOT
	klog_event(KLOG_EVENT_BOOT_INFO, 0, KLOG_LEVEL_WARN, component, message);
#else
	boot_emit_raw("WARN", component, message);
#endif
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
