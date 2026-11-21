/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_logging.c
 * Description: Formatted klog_* calls filter on the klog level and land in dmesg.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <string.h>

/* Every record of the dmesg ring rendered at its 256-byte line cap. */
#define KTEST_KLOG_DUMP_BYTES (1024U * 256U + 1U)

/* 1 when @needle is in the dmesg ring, 0 when not, -1 on OOM. */
static int ktest_dmesg_has(const char *needle)
{
	char *dump = kmalloc(KTEST_KLOG_DUMP_BYTES);
	int found;

	if (!dump)
		return -1;
	(void)klog_read_records(dump, KTEST_KLOG_DUMP_BYTES);
	found = strstr(dump, needle) != NULL;
	kfree(dump);
	return found;
}

/*
 * One level filter: klog_debug_fmt() must follow ir0.loglevel, with no
 * private threshold pinning formatted calls at INFO.
 */
void ktest_log_fmt_follows_klog_level(void)
{
	klog_level_t saved = klog_get_level();

	KTEST_BEGIN("log_fmt_follows_klog_level");

	klog_set_level(KLOG_LEVEL_INFO);
	klog_debug_fmt("KTEST", "log-fmt-filtered-%d", 4243);
	KASSERT_EQ(ktest_dmesg_has("log-fmt-filtered-4243"), 0);

	klog_set_level(KLOG_LEVEL_DEBUG);
	klog_debug_fmt("KTEST", "log-fmt-debug-%d", 4242);
	klog_set_level(saved);
	KASSERT_EQ(ktest_dmesg_has("log-fmt-debug-4242"), 1);

	KTEST_END();
}
