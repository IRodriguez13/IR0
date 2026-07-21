/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: assert.c
 * Description: KTM assertion results → transport + suite counters.
 * Happy-path loops can use ASSERT_BATCH to collapse N× ASSERT_PASS.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ktm.h>
#include <ir0/serial_io.h>
#include <string.h>

static int g_assert_batch_active;
static const char *g_assert_batch_name;
static unsigned g_assert_batch_n;
static unsigned g_assert_batch_pass;
static unsigned g_assert_batch_fail;

void ktm_assert_batch_begin(const char *name)
{
	g_assert_batch_active = 1;
	g_assert_batch_name = name ? name : "batch";
	g_assert_batch_n = 0;
	g_assert_batch_pass = 0;
	g_assert_batch_fail = 0;
}

void ktm_assert_batch_end(void)
{
	char status[48];
	unsigned n = g_assert_batch_n;
	unsigned i;
	unsigned v;
	char *p;

	if (!g_assert_batch_active)
		return;

	/* status: iterations=<n>|PASS or |FAIL */
	memcpy(status, "iterations=", 11);
	p = status + 11;
	if (n == 0)
	{
		*p++ = '0';
	}
	else
	{
		char tmp[12];

		i = 0;
		v = n;
		while (v > 0 && i < sizeof(tmp))
		{
			tmp[i++] = (char)('0' + (v % 10));
			v /= 10;
		}
		while (i > 0)
			*p++ = tmp[--i];
	}
	if (g_assert_batch_fail == 0)
		memcpy(p, "|PASS", 6);
	else
		memcpy(p, "|FAIL", 6);

	ktm_transport_emit("ASSERT_BATCH", g_assert_batch_name, status);
	klog_debug_fmt("KTM", "ASSERT_BATCH %s iterations=%u pass=%u fail=%u",
		       g_assert_batch_name, g_assert_batch_n,
		       g_assert_batch_pass, g_assert_batch_fail);

	g_assert_batch_active = 0;
	g_assert_batch_name = NULL;
	g_assert_batch_n = 0;
	g_assert_batch_pass = 0;
	g_assert_batch_fail = 0;
}

void ktm_assert_result(bool pass, const char *expr, const char *file,
		       unsigned line, const char *message)
{
	ktm_context_t *ctx = ktm_current_context();

	(void)file;
	(void)line;
	(void)message;

	if (g_assert_batch_active)
		g_assert_batch_n++;

	if (pass)
	{
		if (ctx)
			ctx->asserts_pass++;
		if (g_assert_batch_active)
		{
			g_assert_batch_pass++;
			ktm_event_emit4(KTM_EVENT_ASSERT_PASS, KTM_SUBSYS_TEST,
					0, 0, 0, 0);
			/* Suppress per-iteration ASSERT_PASS on serial. */
			return;
		}
		ktm_event_emit4(KTM_EVENT_ASSERT_PASS, KTM_SUBSYS_TEST, 0, 0, 0, 0);
		ktm_transport_emit("ASSERT_PASS", expr ? expr : "?", NULL);
	}
	else
	{
		if (ctx)
			ctx->asserts_fail++;
		if (g_assert_batch_active)
			g_assert_batch_fail++;
		ktm_event_emit4(KTM_EVENT_ASSERT_FAIL, KTM_SUBSYS_TEST, 0, 0, 0, 0);
		ktm_transport_emit("ASSERT_FAIL", expr ? expr : "?", message);
		ktm_fail_at(message ? message : "ASSERT", expr, file, line);
	}
}

void ktm_assert_no_process_leak(const ktm_system_snapshot_t *before,
				const ktm_system_snapshot_t *after)
{
	bool ok;

	if (!before || !after)
	{
		ktm_assert_result(false, "snapshot", KTM_FILE, __LINE__, "null");
		return;
	}
	ok = (after->processes <= before->processes) &&
	     (after->zombies <= before->zombies);
	ktm_assert_result(ok, "no_process_leak", KTM_FILE, __LINE__, NULL);
}

void ktm_assert_no_frame_leak(const ktm_system_snapshot_t *before,
			      const ktm_system_snapshot_t *after)
{
	bool ok;

	if (!before || !after)
	{
		ktm_assert_result(false, "snapshot", KTM_FILE, __LINE__, "null");
		return;
	}
	/* Allow small allocator noise (±2 frames) during boot scenario. */
	ok = (after->used_frames <= before->used_frames + 2);
	ktm_assert_result(ok, "no_frame_leak", KTM_FILE, __LINE__, NULL);
}
