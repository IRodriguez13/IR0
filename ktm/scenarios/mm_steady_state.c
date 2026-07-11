/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_steady_state.c
 * Description: KTM scenario — steady-state frame audit (bounded growth).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"

extern void paging_fase47_steady_state_audit(const char *tag, uint64_t frames_baseline,
					     uint64_t mm_created, uint64_t mm_destroyed);

static int scenario_mm_steady_state_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	uint64_t frames_baseline;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	frames_baseline = before.used_frames;

	paging_fase47_steady_state_audit("ktm.mm.steady_state", frames_baseline, 0, 0);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	/* Audit must not explode frame accounting (±64). */
	KTM_V1_ASSERT_TRUE(after.used_frames <= frames_baseline + 64);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("mm.frames", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_mm_steady_state = {
	.name = "mm.steady_state",
	.flags = 0,
	.setup = NULL,
	.run = scenario_mm_steady_state_run,
	.teardown = NULL,
};

void ktm_scenario_register_mm_steady_state(void)
{
	(void)ktm_scenario_register(&scenario_mm_steady_state);
}
