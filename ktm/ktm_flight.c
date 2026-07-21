/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_flight.c
 * Description: KTM flight recorder — typed ring buffer dumped on panic
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm.h>
#include <config.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <stdint.h>

#if defined(CONFIG_KTM_FLIGHT) && CONFIG_KTM_FLIGHT

#define KTM_FLIGHT_CAP 256

struct ktm_flight_entry
{
	uint32_t seq;
	uint16_t type;
	uint16_t cpu;
	uint32_t pid;
	uint32_t a0;
	uint32_t a1;
	uint32_t a2;
	uint32_t a3;
};

static struct ktm_flight_entry ktm_flight_ring[KTM_FLIGHT_CAP];
static uint32_t ktm_flight_head;
static uint32_t ktm_flight_seq;

static const char *ktm_flight_type_name(uint16_t type)
{
	switch (type)
	{
	case KTM_FL_SYSCALL_ENTER:
		return "syscall_enter";
	case KTM_FL_SYSCALL_RET:
		return "syscall_ret";
	case KTM_FL_SCHED_SWITCH:
		return "sched_switch";
	case KTM_FL_PANIC:
		return "panic";
	case KTM_FL_INVARIANT:
		return "invariant";
	case KTM_FL_EV:
		return "ev";
	case KTM_FL_PF_USER:
		return "pf_user";
	case KTM_FL_SIGNAL_DELIVER:
		return "signal_deliver";
	default:
		return "unknown";
	}
}

void ktm_flight_record(uint16_t type, uint32_t a0, uint32_t a1,
		       uint32_t a2, uint32_t a3)
{
	struct ktm_flight_entry *slot;
	uint32_t idx;

	idx = ktm_flight_head % KTM_FLIGHT_CAP;
	slot = &ktm_flight_ring[idx];
	ktm_flight_head++;
	ktm_flight_seq++;

	slot->seq = ktm_flight_seq;
	slot->type = type;
	slot->cpu = 0;
	slot->pid = current_process ? (uint32_t)current_process->task.pid : 0;
	slot->a0 = a0;
	slot->a1 = a1;
	slot->a2 = a2;
	slot->a3 = a3;
}

void ktm_flight_dump(void)
{
	ktm_flight_dump_last(0);
}

void ktm_flight_dump_last(uint32_t max_events)
{
	uint32_t count;
	uint32_t start_seq;
	uint32_t i;

	if (ktm_flight_head == 0)
		return;

	count = ktm_flight_head;
	if (count > KTM_FLIGHT_CAP)
		count = KTM_FLIGHT_CAP;

	if (max_events > 0 && max_events < count)
		count = max_events;

	start_seq = ktm_flight_seq - count + 1;

	klog_debug_fmt("KTM", "--- KTM FLIGHT RECORDER (last %x events%s) ---",
		       (unsigned)count, max_events > 0 ? ", capped" : "");

	for (i = 0; i < count; i++)
	{
		uint32_t seq = start_seq + i;
		uint32_t idx = (ktm_flight_head - count + i) % KTM_FLIGHT_CAP;
		const struct ktm_flight_entry *e = &ktm_flight_ring[idx];

		klog_debug_fmt("KTM",
			       "[%x] cpu%x pid=%x %s a0=%x a1=%x a2=%x a3=%x",
			       (unsigned)seq, (unsigned)(uint32_t)e->cpu,
			       (unsigned)e->pid, ktm_flight_type_name(e->type),
			       (unsigned)e->a0, (unsigned)e->a1, (unsigned)e->a2,
			       (unsigned)e->a3);
	}

	klog_debug_fmt("KTM", "--- END KTM FLIGHT RECORDER ---");
}

#else /* !CONFIG_KTM_FLIGHT */

void ktm_flight_record(uint16_t type, uint32_t a0, uint32_t a1,
		       uint32_t a2, uint32_t a3)
{
	(void)type;
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

void ktm_flight_dump(void)
{
}

void ktm_flight_dump_last(uint32_t max_events)
{
	(void)max_events;
}

#endif /* CONFIG_KTM_FLIGHT */
