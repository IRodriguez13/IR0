/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: host_klog_stub.c
 * Description: No-op klog stubs for host-linked kernel sources
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/ktm/klog.h>
#include <stdarg.h>

void klog_boot_hold(int on)
{
	(void)on;
}

void klog_set_level(klog_level_t level)
{
	(void)level;
}

klog_level_t klog_get_level(void)
{
	return KLOG_LEVEL_INFO;
}

void klog_set_protocol_mirror(klog_protocol_mirror_fn fn)
{
	(void)fn;
}

void klog_print(const char *str)
{
	(void)str;
}

void klog_hex32(uint32_t num)
{
	(void)num;
}

void klog_hex64(uint64_t num)
{
	(void)num;
}

void klog_smoke(const char *tag)
{
	(void)tag;
}

void klog_debug(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_info(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_warn(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_error(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_fatal(const char *component, const char *message)
{
	(void)component;
	(void)message;
}

void klog_debug_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_info_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_warn_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_error_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}

void klog_fatal_fmt(const char *component, const char *format, ...)
{
	(void)component;
	(void)format;
}
