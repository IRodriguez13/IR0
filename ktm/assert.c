/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: assert.c
 * Description: KTM assertion results → transport + suite counters.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ktm.h>
#include <ir0/serial_io.h>

void ktm_assert_result(bool pass, const char *expr, const char *file,
		       unsigned line, const char *message)
{
	ktm_context_t *ctx = ktm_current_context();

	(void)file;
	(void)line;
	(void)message;

	if (pass)
	{
		if (ctx)
			ctx->asserts_pass++;
		ktm_event_emit4(KTM_EVENT_ASSERT_PASS, KTM_SUBSYS_TEST, 0, 0, 0, 0);
		ktm_transport_emit("ASSERT_PASS", expr ? expr : "?", NULL);
	}
	else
	{
		if (ctx)
			ctx->asserts_fail++;
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
