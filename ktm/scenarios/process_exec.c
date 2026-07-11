/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_exec.c
 * Description: KTM scenario — exec checkpoint + snapshot stability (no ELF load).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>

static int scenario_process_exec_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXEC);
	ktm_event_emit4(KTM_EVENT_INFO, KTM_SUBSYS_PROC, 0, 0, 0, 0);
	KTM_REQUIRE(ktm_invariants_run(KTM_INV_PROCESS | KTM_INV_FRAMES) == 0);
	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_process_exec = {
	.name = "process.exec",
	.flags = 0,
	.setup = NULL,
	.run = scenario_process_exec_run,
	.teardown = NULL,
};

void ktm_scenario_register_process_exec(void)
{
	(void)ktm_scenario_register(&scenario_process_exec);
}
