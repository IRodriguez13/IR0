/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_sched_backend_contract.c
 * Description: sched_backend_contract_suite — ir0_sched_ops mandatory hooks (§7).
 */

#include "test_harness_ir0.h"
#include <stddef.h>
#include <string.h>

/*
 * Host cannot link rr/priority backends easily; validate the ops-table
 * contract shape that sched/sched_ops.h documents.
 */
struct host_sched_ops
{
	void (*add)(void *proc);
	void (*remove)(void *proc);
	void (*schedule_next)(void);
	int (*count_runnable)(void);
	void (*promote)(void *proc);
};

static int g_add;
static int g_rm;
static int g_sched;
static int g_promote;
static int g_runnable;

static void hop_add(void *p)
{
	(void)p;
	g_add++;
}

static void hop_remove(void *p)
{
	(void)p;
	g_rm++;
}

static void hop_schedule_next(void)
{
	g_sched++;
}

static int hop_count_runnable(void)
{
	return g_runnable;
}

static void hop_promote(void *p)
{
	(void)p;
	g_promote++;
}

static int sched_ops_complete(const struct host_sched_ops *ops)
{
	return ops && ops->add && ops->remove && ops->schedule_next &&
	       ops->count_runnable && ops->promote;
}

void test_sched_backend_contract(void)
{
	struct host_sched_ops ops;

	TEST_BEGIN("sched_backend_contract_mandatory_ops");
	memset(&ops, 0, sizeof(ops));
	ASSERT(!sched_ops_complete(&ops));
	ops.add = hop_add;
	ops.remove = hop_remove;
	ops.schedule_next = hop_schedule_next;
	ops.count_runnable = hop_count_runnable;
	ops.promote = hop_promote;
	ASSERT(sched_ops_complete(&ops));
	g_runnable = 2;
	ops.add(NULL);
	ops.promote(NULL);
	ops.schedule_next();
	ops.remove(NULL);
	ASSERT_EQ(g_add, 1);
	ASSERT_EQ(g_promote, 1);
	ASSERT_EQ(g_sched, 1);
	ASSERT_EQ(g_rm, 1);
	ASSERT_EQ(ops.count_runnable(), 2);
	TEST_END();
}
