/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: event_ring.c
 * Description: Typed KTM event ring + legacy string wrappers + consumer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ktm.h>
#include <config.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#define KTM_EVENT_RING_CAP 256

static ktm_event_t g_ring[KTM_EVENT_RING_CAP];
static uint32_t g_head; /* next write index (monotonic) */
static uint32_t g_tail; /* next read index for consumers */
static uint64_t g_seq;
static ktm_context_t *g_ctx;

ktm_context_t *ktm_current_context(void)
{
	return g_ctx;
}

void ktm_set_current_context(ktm_context_t *ctx)
{
	g_ctx = ctx;
}

uint64_t ktm_now_ticks(void)
{
	return g_seq; /* monotonic enough for v1 ordering */
}

int32_t ktm_current_pid(void)
{
	extern process_t *current_process;

	return current_process ? (int32_t)current_process->task.pid : 0;
}

void ktm_event_emit4(uint16_t type, uint16_t subsystem,
		     uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
	ktm_event_t *slot;
	uint32_t idx;

#if !(defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS)
	(void)type;
	(void)subsystem;
	(void)arg0;
	(void)arg1;
	(void)arg2;
	(void)arg3;
	return;
#else
	g_seq++;
	idx = g_head % KTM_EVENT_RING_CAP;
	slot = &g_ring[idx];
	g_head++;

	/* Drop oldest unread if ring full. */
	if ((uint32_t)(g_head - g_tail) > KTM_EVENT_RING_CAP)
		g_tail = g_head - KTM_EVENT_RING_CAP;

	slot->sequence = g_seq;
	slot->timestamp = g_seq;
	slot->cpu = 0;
	slot->pid = ktm_current_pid();
	slot->type = type;
	slot->subsystem = subsystem;
	slot->arg0 = arg0;
	slot->arg1 = arg1;
	slot->arg2 = arg2;
	slot->arg3 = arg3;

	KTM_FLIGHT((uint16_t)type, (uint32_t)arg0, (uint32_t)arg1,
		   (uint32_t)arg2, (uint32_t)arg3);

	(void)slot;
#endif
}

int ktm_event_pending(void)
{
#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS
	return (g_head != g_tail) ? 1 : 0;
#else
	return 0;
#endif
}

void ktm_event_ring_reset_cursor(void)
{
	g_tail = g_head;
}

int ktm_event_copy_out(ktm_event_t *dst, size_t max_events)
{
	size_t n = 0;

	if (!dst || max_events == 0)
		return 0;
#if !(defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS)
	(void)dst;
	(void)max_events;
	return 0;
#else
	while (n < max_events && g_tail != g_head)
	{
		dst[n++] = g_ring[g_tail % KTM_EVENT_RING_CAP];
		g_tail++;
	}
	return (int)n;
#endif
}

#if defined(CONFIG_KTM_EVENTS) && CONFIG_KTM_EVENTS

void ktm_event_emit(const char *tag)
{
	klog_debug_fmt("KTM", "[KTM][EV] %s", tag ? tag : "(null)");
	ktm_event_emit4(KTM_EVENT_INFO, KTM_SUBSYS_CORE, 0, 0, 0, 0);
	ktm_transport_emit("EV", tag ? tag : "(null)", NULL);
}

void ktm_event_emit_pid(const char *tag, uint32_t pid)
{
	klog_debug_fmt("KTM", "[KTM][EV] %s pid=%x", tag ? tag : "(null)",
		       (unsigned)pid);
	ktm_event_emit4(KTM_EVENT_INFO, KTM_SUBSYS_CORE, pid, 0, 0, 0);
	ktm_transport_emit("EV", tag ? tag : "(null)", NULL);
}

#endif /* CONFIG_KTM_EVENTS */
