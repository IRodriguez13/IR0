/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_oom_class.c
 * Description: KTM scenario — OOM class audit hook.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"

extern void paging_fase43_oom_audit(const char *tag);

static int scenario_mm_oom_class_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	paging_fase43_oom_audit("ktm.mm.oom_class");
	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_V1_ASSERT_TRUE(after.used_frames <= before.used_frames + 64);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("mm.frames", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_mm_oom_class = {
	.name = "mm.oom_class",
	.flags = 0,
	.setup = NULL,
	.run = scenario_mm_oom_class_run,
	.teardown = NULL,
};

void ktm_scenario_register_mm_oom_class(void)
{
	(void)ktm_scenario_register(&scenario_mm_oom_class);
}
