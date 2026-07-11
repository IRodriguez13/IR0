/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_lifecycle.c
 * Description: Pilot scenario — snapshot + invariants + synthetic process destroy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

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

static int scenario_process_lifecycle_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	process_t *child;
	pid_t pid;

	(void)ctx;
	/*
	 * Boot-time suite may run before any task is linked; baseline can be 0.
	 * Leak checks compare after synthetic create+reap against this snapshot.
	 */
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	KTM_REQUIRE(ktm_invariants_run(KTM_INV_PROCESS | KTM_INV_FRAMES) == 0);

	/*
	 * Synthetic zombie: allocate a process_t, link it, destroy via
	 * process_destroy (reap path). Avoids needing a scheduled child.
	 */
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
	ktm_event_emit4(KTM_EVENT_PROCESS_CREATE, KTM_SUBSYS_PROC, (uint64_t)pid, 0, 0, 0);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);
	KTM_V1_ASSERT_TRUE(process_exists(pid));

	process_reap_zombie_child(child);
	ktm_event_emit4(KTM_EVENT_PROCESS_REAP, KTM_SUBSYS_PROC, (uint64_t)pid, 0, 0, 0);
	KTM_V1_ASSERT_TRUE(!process_exists(pid));

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);

	(void)ktm_probe_run("mm.frames", NULL);
	(void)ktm_probe_run("proc.list", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_process_lifecycle = {
	.name = "process.lifecycle",
	.flags = 0,
	.setup = NULL,
	.run = scenario_process_lifecycle_run,
	.teardown = NULL,
};

void ktm_scenario_register_pipe_lifecycle(void);
void ktm_scenario_register_mm_cow_fork(void);
void ktm_scenario_register_process_exec(void);
void ktm_scenario_register_process_fork_rollback(void);

void ktm_scenarios_register_builtins(void)
{
	(void)ktm_scenario_register(&scenario_process_lifecycle);
	ktm_scenario_register_pipe_lifecycle();
	ktm_scenario_register_mm_cow_fork();
	ktm_scenario_register_process_exec();
	ktm_scenario_register_process_fork_rollback();
}
