/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_wait_drain.c
 * Description: KTM scenario — synthetic zombie drain / reap (wait-drain MVP).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

#define WAIT_DRAIN_N 8

extern process_t *process_list;
extern void process_reap_zombie_child(process_t *child);
extern pid_t process_get_next_pid(void);

static int process_exists(pid_t pid)
{
	process_t *p;

	for (p = process_list; p; p = p->next)
	{
		if (p->task.pid == pid)
			return 1;
	}
	return 0;
}

static int scenario_process_wait_drain_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	process_t *kids[WAIT_DRAIN_N];
	pid_t pids[WAIT_DRAIN_N];
	int i;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	for (i = 0; i < WAIT_DRAIN_N; i++)
	{
		kids[i] = kmalloc_try(sizeof(process_t));
		KTM_REQUIRE(kids[i] != NULL);
		memset(kids[i], 0, sizeof(*kids[i]));
		pids[i] = process_get_next_pid();
		kids[i]->task.pid = pids[i];
		kids[i]->tgid = pids[i];
		kids[i]->state = PROCESS_ZOMBIE;
		kids[i]->exit_code = 0;
		kids[i]->next = process_list;
		process_list = kids[i];
		KTM_V1_ASSERT_TRUE(process_exists(pids[i]));
	}

	KTM_CHECKPOINT(KTM_CP_PROCESS_REAP);
	for (i = 0; i < WAIT_DRAIN_N; i++)
	{
		process_reap_zombie_child(kids[i]);
		KTM_V1_ASSERT_TRUE(!process_exists(pids[i]));
	}

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("proc.list", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_process_wait_drain = {
	.name = "process.wait_drain",
	.flags = 0,
	.setup = NULL,
	.run = scenario_process_wait_drain_run,
	.teardown = NULL,
};

void ktm_scenario_register_process_wait_drain(void)
{
	(void)ktm_scenario_register(&scenario_process_wait_drain);
}
