/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_fork_rollback.c
 * Description: KTM scenario — failed fork path leaves no process leak.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

extern process_t *process_list;
extern void process_reap_zombie_child(process_t *child);
extern pid_t process_get_next_pid(void);

static int scenario_process_fork_rollback_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	process_t *child;
	pid_t pid;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	/*
	 * Simulate fork failure after allocating the child struct: never link
	 * permanently — free without inserting, assert no leak.
	 */
	child = kmalloc_try(sizeof(process_t));
	KTM_REQUIRE(child != NULL);
	memset(child, 0, sizeof(*child));
	pid = process_get_next_pid();
	child->task.pid = pid;
	KTM_CHECKPOINT(KTM_CP_PROCESS_FORK);
	kfree(child);
	ktm_event_emit4(KTM_EVENT_WARN, KTM_SUBSYS_PROC, (uint64_t)pid, 1, 0, 0);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	KTM_V1_ASSERT_TRUE(after.processes == before.processes);
	return KTM_OK;
}

static const ktm_scenario_t scenario_process_fork_rollback = {
	.name = "process.fork_rollback",
	.flags = 0,
	.setup = NULL,
	.run = scenario_process_fork_rollback_run,
	.teardown = NULL,
};

void ktm_scenario_register_process_fork_rollback(void)
{
	(void)ktm_scenario_register(&scenario_process_fork_rollback);
}
