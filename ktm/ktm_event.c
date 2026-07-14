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
#include <ir0/serial_io.h>

#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS

void ktm_event_emit(const char *tag)
{
	serial_print("[KTM][EV] ");
	serial_print(tag ? tag : "(null)");
	serial_print("\n");
	KTM_FLIGHT(KTM_FL_EV, 0, 0, 0, 0);
}

void ktm_event_emit_pid(const char *tag, uint32_t pid)
{
	serial_print("[KTM][EV] ");
	serial_print(tag ? tag : "(null)");
	serial_print(" pid=");
	serial_print_hex32(pid);
	serial_print("\n");
	KTM_FLIGHT(KTM_FL_EV, pid, 0, 0, 0);
}

#endif /* CONFIG_KTM_EVENTS */
