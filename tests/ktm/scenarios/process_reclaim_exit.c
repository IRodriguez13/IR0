/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_reclaim_exit.c
 * Description: KTM scenario — repeated zombie create/reap (exit reclaim MVP).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

#define RECLAIM_ROUNDS 64

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

static int scenario_process_reclaim_exit_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	int round;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	ktm_assert_batch_begin("process.reclaim_exit");
	for (round = 0; round < RECLAIM_ROUNDS; round++)
	{
		process_t *child;
		pid_t pid;

		child = kmalloc_try(sizeof(process_t));
		KTM_REQUIRE(child != NULL);
		memset(child, 0, sizeof(*child));
		pid = process_get_next_pid();
		child->task.pid = pid;
		child->tgid = pid;
		child->state = PROCESS_ZOMBIE;
		child->exit_code = 0;
		child->next = process_list;
		process_list = child;
		KTM_V1_ASSERT_TRUE(process_exists(pid));

		KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);
		KTM_CHECKPOINT(KTM_CP_PROCESS_REAP);
		process_reap_zombie_child(child);
		KTM_V1_ASSERT_TRUE(!process_exists(pid));
	}
	ktm_assert_batch_end();

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("mm.frames", NULL);
	(void)ktm_probe_run("proc.list", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_process_reclaim_exit = {
	.name = "process.reclaim_exit",
	.flags = 0,
	.setup = NULL,
	.run = scenario_process_reclaim_exit_run,
	.teardown = NULL,
};

void ktm_scenario_register_process_reclaim_exit(void)
{
	(void)ktm_scenario_register(&scenario_process_reclaim_exit);
}
