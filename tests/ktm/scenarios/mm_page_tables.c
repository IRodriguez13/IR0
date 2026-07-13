/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_page_tables.c
 * Description: KTM scenario — IR0 MM category frame/PT balance + reclaim churn.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

#define PT_CHURN_ROUNDS 32

extern void paging_ir0_mm_category_stats(uint64_t *user_alloc, uint64_t *user_free,
					 uint64_t *pt_alloc, uint64_t *pt_free,
					 uint64_t *kernel_alloc, uint64_t *kernel_free);
extern process_t *process_list;
extern void process_reap_zombie_child(process_t *child);
extern pid_t process_get_next_pid(void);

static void assert_category_balance(void)
{
	uint64_t user_alloc = 0;
	uint64_t user_free = 0;
	uint64_t pt_alloc = 0;
	uint64_t pt_free = 0;
	uint64_t kernel_alloc = 0;
	uint64_t kernel_free = 0;

	paging_ir0_mm_category_stats(&user_alloc, &user_free, &pt_alloc, &pt_free,
				     &kernel_alloc, &kernel_free);
	KTM_V1_ASSERT_TRUE(user_alloc >= user_free);
	KTM_V1_ASSERT_TRUE(pt_alloc >= pt_free);
	KTM_V1_ASSERT_TRUE(kernel_alloc >= kernel_free);
}

static int scenario_mm_page_tables_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	int round;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	assert_category_balance();

	/*
	 * Churn synthetic zombies (create+reap) between category samples so the
	 * gate is stricter than a single static counter read (PT depth intent).
	 */
	for (round = 0; round < PT_CHURN_ROUNDS; round++)
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
		process_reap_zombie_child(child);
	}

	assert_category_balance();

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("mm.frames", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_mm_page_tables = {
	.name = "mm.page_tables",
	.flags = 0,
	.setup = NULL,
	.run = scenario_mm_page_tables_run,
	.teardown = NULL,
};

void ktm_scenario_register_mm_page_tables(void)
{
	(void)ktm_scenario_register(&scenario_mm_page_tables);
}
