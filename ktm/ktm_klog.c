/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_klog.c
 * Description: KTM log policy — optional KTM|LOG| mirror of human klog lines.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <config.h>
#include <stddef.h>
#include <ir0/klog.h>
#include <ir0/ktm/transport.h>

#if defined(CONFIG_KTM_SERIAL_VERBOSE) && CONFIG_KTM_SERIAL_VERBOSE

static const char *ktm_klog_level_name(klog_level_t level)
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
		return "?";
	}
}

static void ktm_klog_mirror(klog_level_t level, const char *component,
			    const char *message)
{
	char name[64];
	const char *comp = component ? component : "?";
	const char *lv = ktm_klog_level_name(level);
	size_t i = 0;

	while (*lv && i + 1 < sizeof(name))
		name[i++] = *lv++;
	if (i + 1 < sizeof(name))
		name[i++] = ':';
	while (*comp && i + 1 < sizeof(name))
		name[i++] = *comp++;
	name[i] = '\0';

	ktm_transport_emit("LOG", name, message ? message : "");
}

void ktm_klog_init(void)
{
	klog_set_protocol_mirror(ktm_klog_mirror);
}

#else /* !CONFIG_KTM_SERIAL_VERBOSE */

void ktm_klog_init(void)
{
}

#endif
