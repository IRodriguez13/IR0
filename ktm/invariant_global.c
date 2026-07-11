/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: invariant_global.c
 * Description: Cheap global invariants (process list consistency).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/process.h>

extern process_t *process_list;

int ktm_invariants_run(uint32_t mask)
{
	process_t *p;
	int bad = 0;

	if (mask & KTM_INV_PROCESS)
	{
		for (p = process_list; p; p = p->next)
		{
			if (p->task.pid <= 0)
			{
				ktm_assert_result(false, "pid>0", KTM_FILE, __LINE__,
						  "invariant");
				bad++;
			}
			if (p->state == PROCESS_ZOMBIE && p->task.pid == 0)
			{
				ktm_assert_result(false, "zombie_pid", KTM_FILE, __LINE__,
						  "invariant");
				bad++;
			}
		}
		if (bad == 0)
			ktm_transport_emit("INVARIANT", "process.list", "PASS");
	}
	if (mask & KTM_INV_FRAMES)
	{
		ktm_system_snapshot_t snap;

		if (ktm_snapshot_take(&snap) == 0 &&
		    snap.used_frames <= snap.total_frames)
			ktm_transport_emit("INVARIANT", "mm.frames", "PASS");
		else
		{
			ktm_assert_result(false, "frames_bounds", KTM_FILE, __LINE__,
					  "invariant");
			bad++;
		}
	}
	return bad == 0 ? 0 : -1;
}
