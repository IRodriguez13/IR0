/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_cow_fork.c
 * Description: KTM scenario — fork COW intent (parent/child page isolation smoke).
 *
 * Full userspace COW A–F remains in setup/pid1; this scenario validates
 * kernel-visible fork checkpoint + frame non-explosion after synthetic fork
 * bookkeeping. Deep COW data-plane stays in smoke-mm-cow-lazy until libktm case.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

extern process_t *process_list;
extern void process_reap_zombie_child(process_t *child);
extern pid_t process_get_next_pid(void);

static int scenario_mm_cow_fork_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t mid;
	ktm_system_snapshot_t after;
	process_t *child;
	pid_t pid;
	uint64_t frames_before;
	uint64_t frames_mid;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	frames_before = before.used_frames;

	child = kmalloc_try(sizeof(process_t));
	KTM_REQUIRE(child != NULL);
	memset(child, 0, sizeof(*child));
	pid = process_get_next_pid();
	child->task.pid = pid;
	child->tgid = pid;
	child->ppid = 0;
	child->state = PROCESS_ZOMBIE;
	child->exit_code = 0;
	child->next = process_list;
	process_list = child;

	KTM_CHECKPOINT(KTM_CP_PROCESS_FORK);
	ktm_event_emit4(KTM_EVENT_PROCESS_CREATE, KTM_SUBSYS_MM, (uint64_t)pid, 0, 0, 0);

	KTM_REQUIRE(ktm_snapshot_take(&mid) == 0);
	frames_mid = mid.used_frames;
	/* Synthetic link must not explode frame accounting (±64). */
	KTM_V1_ASSERT_TRUE(frames_mid <= frames_before + 64);

	KTM_CHECKPOINT(KTM_CP_PROCESS_REAP);
	process_reap_zombie_child(child);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("mm.frames", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_mm_cow_fork = {
	.name = "mm.cow_fork",
	.flags = 0,
	.setup = NULL,
	.run = scenario_mm_cow_fork_run,
	.teardown = NULL,
};

void ktm_scenario_register_mm_cow_fork(void)
{
	(void)ktm_scenario_register(&scenario_mm_cow_fork);
}
