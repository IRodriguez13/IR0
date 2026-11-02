/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: test_oom_policy.c
 * Description: Host contracts for deterministic OOM victim selection.
 */

#include "test_harness.h"
#include <ir0/oom.h>
#include <ir0/errno.h>

#define ELIGIBLE (IR0_OOM_CANDIDATE_USER | IR0_OOM_CANDIDATE_ALIVE | \
                  IR0_OOM_CANDIDATE_HAS_MM)

void test_oom_policy(void)
{
    struct ir0_oom_candidate candidates[] = {
        { .pid = 1, .flags = ELIGIBLE, .resident_pages = 900 },
        { .pid = 20, .flags = ELIGIBLE, .resident_pages = 100 },
        { .pid = 30, .flags = ELIGIBLE, .resident_pages = 300 },
        { .pid = 40, .flags = ELIGIBLE | IR0_OOM_CANDIDATE_SKIP,
          .resident_pages = 1000 },
    };
    size_t selected = (size_t)-1;

    TEST_BEGIN("oom_policy_selects_largest_eligible_mm");
    ASSERT_EQ(oom_select_candidate(candidates, 4, 4096, &selected), 0);
    ASSERT_EQ(selected, (size_t)2);
    TEST_END();

    TEST_BEGIN("oom_policy_honors_adjustment_and_origin");
    candidates[1].score_adj = IR0_OOM_SCORE_ADJ_MAX;
    ASSERT(oom_candidate_badness(&candidates[1], 4096) >
           oom_candidate_badness(&candidates[2], 4096));
    candidates[2].flags |= IR0_OOM_CANDIDATE_ORIGIN;
    ASSERT_EQ(oom_candidate_badness(&candidates[2], 4096), UINT64_MAX);
    TEST_END();

    TEST_BEGIN("oom_policy_rejects_protected_tasks");
    candidates[1].score_adj = IR0_OOM_SCORE_ADJ_MIN;
    candidates[2].flags = IR0_OOM_CANDIDATE_SKIP;
    ASSERT_EQ(oom_select_candidate(candidates, 4, 4096, &selected), -ESRCH);
    TEST_END();
}
