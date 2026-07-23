/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: klog.c
 * Description: KTM kernel logging hub — kprintf + levelled human channel.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ir0/ktm/klog.h>
#include <ir0/serial_io.h>
#include <ir0/clock.h>

static klog_level_t g_klog_level = KLOG_LEVEL_INFO;
static klog_protocol_mirror_fn g_protocol_mirror;
/* Suppress serial until BOOT banner (see klog_boot_hold in kmain). */
static int g_boot_hold = 1;

void klog_boot_hold(int on)
{
	g_boot_hold = on ? 1 : 0;
}

static const char *klog_level_string(klog_level_t level)
{
	switch (level)
	{
	case KLOG_LEVEL_DEBUG:
		return "DEBUG";
	case KLOG_LEVEL_INFO:
		return "INFO";
	case KLOG_LEVEL_WARN:
		return "WARN";
	case KLOG_LEVEL_ERROR:
		return "ERROR";
	case KLOG_LEVEL_FATAL:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

static void klog_write_raw(const char *str)
{
	/*
	 * Serial only. Mirroring to console_backend here re-enters UI paths and
	 * duplicated every line on -serial stdio smokes. Screen output stays on
	 * console_puts/typewriter; use kprintf only as the log channel.
	 */
	if (!str || g_boot_hold)
		return;
	serial_print(str);
}

static void klog_append_timestamp(char *out, size_t out_sz, size_t *off)
{
	uint64_t uptime_ms;
	uint64_t seconds;
	uint32_t milliseconds;
	char sec_buf[24];
	char rev[24];
	int rev_len = 0;
	int sec_len = 0;
	size_t i;

	if (*off >= out_sz)
		return;

	if (!clock_is_ready())
	{
		const char *unk = "[    ?.???] ";

		for (i = 0; unk[i] && *off + 1 < out_sz; i++)
			out[(*off)++] = unk[i];
		out[*off] = '\0';
		return;
	}

	/*
	 * Format by hand — never rely on %llu here. Corrupted timestamps must
	 * not enter the human channel / dmesg ring.
	 */
	uptime_ms = clock_get_uptime_milliseconds();
	seconds = uptime_ms / 1000;
	milliseconds = (uint32_t)(uptime_ms % 1000);

	if (seconds == 0)
	{
		sec_buf[sec_len++] = '0';
	}
	else
	{
		uint64_t t = seconds;

		while (t > 0 && rev_len < (int)sizeof(rev))
		{
			rev[rev_len++] = (char)('0' + (t % 10));
			t /= 10;
		}
		while (rev_len > 0)
			sec_buf[sec_len++] = rev[--rev_len];
	}
	sec_buf[sec_len] = '\0';

	if (*off + 1 < out_sz)
		out[(*off)++] = '[';
	for (i = 0; i < (size_t)sec_len && *off + 1 < out_sz; i++)
		out[(*off)++] = sec_buf[i];
	if (*off + 1 < out_sz)
		out[(*off)++] = '.';
	if (*off + 1 < out_sz)
		out[(*off)++] = (char)('0' + ((milliseconds / 100) % 10));
	if (*off + 1 < out_sz)
		out[(*off)++] = (char)('0' + ((milliseconds / 10) % 10));
	if (*off + 1 < out_sz)
		out[(*off)++] = (char)('0' + (milliseconds % 10));
	if (*off + 1 < out_sz)
		out[(*off)++] = ']';
	if (*off + 1 < out_sz)
		out[(*off)++] = ' ';
	out[*off] = '\0';
}

void klog_print(const char *str)
{
	serial_print(str);
}

void klog_hex32(uint32_t num)
{
	serial_print_hex32(num);
}

void klog_hex64(uint64_t num)
{
	serial_print_hex64(num);
}

void klog_set_level(klog_level_t level)
{
	g_klog_level = level;
}

klog_level_t klog_get_level(void)
{
	return g_klog_level;
}

void klog_set_protocol_mirror(klog_protocol_mirror_fn fn)
{
	g_protocol_mirror = fn;
}

void klog_emit(klog_level_t level, const char *component, const char *message)
{
	char line[640];
	size_t off = 0;
	int n;

	if (level < g_klog_level)
		return;
	if (!component)
		component = "?";
	if (!message)
		message = "";

	klog_append_timestamp(line, sizeof(line), &off);
	n = snprintf(line + off, sizeof(line) - off, "[%s] [%s] %s\n",
		     klog_level_string(level), component, message);
	if (n > 0)
		off += (size_t)n;
	if (off >= sizeof(line))
		line[sizeof(line) - 1] = '\0';

	klog_write_raw(line);

	if (g_protocol_mirror)
		g_protocol_mirror(level, component, message);
}

void klog_debug(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_DEBUG, component, message);
}

void klog_info(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_INFO, component, message);
}

void klog_warn(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_WARN, component, message);
}

void klog_error(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_ERROR, component, message);
}

void klog_fatal(const char *component, const char *message)
{
	klog_emit(KLOG_LEVEL_FATAL, component, message);
}

void klog_smoke(const char *tag)
{
	if (!tag)
		tag = "?";
	klog_emit(KLOG_LEVEL_INFO, "SMOKE", tag);
}

static void klog_vfmt(klog_level_t level, const char *component, const char *format,
		      va_list args)
{
	char message[512];

	if (level < g_klog_level)
		return;
	vsnprintf(message, sizeof(message), format, args);
	klog_emit(level, component, message);
}

void klog_debug_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_DEBUG, component, format, args);
	va_end(args);
}

void klog_info_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_INFO, component, format, args);
	va_end(args);
}

void klog_warn_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_WARN, component, format, args);
	va_end(args);
}

void klog_error_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_ERROR, component, format, args);
	va_end(args);
}

void klog_fatal_fmt(const char *component, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	klog_vfmt(KLOG_LEVEL_FATAL, component, format, args);
	va_end(args);
}

int kprintf_level(klog_level_t level, const char *comp, const char *fmt, ...)
{
	va_list args;
	char message[512];
	int n;

	if (level < g_klog_level)
		return 0;
	va_start(args, fmt);
	n = vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	klog_emit(level, comp ? comp : "KERN", message);
	return n;
}

int kvprintf(const char *fmt, va_list ap)
{
	char buf[512];
	int n;

	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	klog_write_raw(buf);
	return n;
}

int kprintf(const char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = kvprintf(fmt, args);
	va_end(args);
	return n;
}
