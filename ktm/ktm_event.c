/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_event.c
 * Description: IR0 kernel source — ktm event
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <ir0/ktm/klog.h>

#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS

#include <config.h>

void ktm_event_emit(const char *tag)
{
#if defined(CONFIG_KTM_SERIAL_VERBOSE) && CONFIG_KTM_SERIAL_VERBOSE
	klog_debug_fmt("KTM", "[KTM][EV] %s", tag ? tag : "(null)");
#else
	(void)tag;
#endif
	KTM_FLIGHT(KTM_FL_EV, 0, 0, 0, 0);
}

void ktm_event_emit_pid(const char *tag, uint32_t pid)
{
#if defined(CONFIG_KTM_SERIAL_VERBOSE) && CONFIG_KTM_SERIAL_VERBOSE
	klog_debug_fmt("KTM", "[KTM][EV] %s pid=%x", tag ? tag : "(null)",
		       (unsigned)pid);
#else
	(void)tag;
#endif
	KTM_FLIGHT(KTM_FL_EV, pid, 0, 0, 0);
}

#endif /* CONFIG_KTM_EVENTS */
