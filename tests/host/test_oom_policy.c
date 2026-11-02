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
    struct ir0_oom_candidate small = {
        .pid = 30, .flags = ELIGIBLE, .resident_pages = 100
    };
    struct ir0_oom_candidate large = {
        .pid = 40, .flags = ELIGIBLE, .resident_pages = 200
    };
    struct ir0_oom_candidate tie_low_pid = {
        .pid = 10, .flags = ELIGIBLE, .resident_pages = 200
    };
    struct ir0_oom_candidate init = {
        .pid = 1, .flags = ELIGIBLE, .resident_pages = 900
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

    TEST_BEGIN("oom_policy_better_orders_like_the_selector");
    ASSERT(oom_candidate_better(&large, &small, 4096));
    ASSERT(!oom_candidate_better(&small, &large, 4096));
    ASSERT(oom_candidate_better(&small, NULL, 4096));
    ASSERT(!oom_candidate_better(NULL, &small, 4096));
    TEST_END();

    TEST_BEGIN("oom_policy_better_breaks_ties_by_lowest_pid");
    ASSERT(oom_candidate_better(&tie_low_pid, &large, 4096));
    ASSERT(!oom_candidate_better(&large, &tie_low_pid, 4096));
    TEST_END();

    TEST_BEGIN("oom_policy_better_skips_ineligible_incumbent");
    ASSERT(!oom_candidate_better(&init, NULL, 4096));
    ASSERT(oom_candidate_better(&small, &init, 4096));
    small.score_adj = IR0_OOM_SCORE_ADJ_MIN;
    ASSERT(!oom_candidate_better(&small, NULL, 4096));
    TEST_END();

    TEST_BEGIN("oom_score_normalises_badness_to_linux_range");
    ASSERT_EQ(oom_badness_score(100, 0, 0, 1000), 101);
    ASSERT_EQ(oom_badness_score(100, 10, 0, 1000), 111);
    ASSERT_EQ(oom_badness_score(2000, 0, 0, 1000), 1000);
    ASSERT_EQ(oom_badness_score(100, 0, IR0_OOM_SCORE_ADJ_MIN, 1000), 0);
    ASSERT_EQ(oom_badness_score(0, 0, IR0_OOM_SCORE_ADJ_MAX, 1000), 1000);
    ASSERT_EQ(oom_badness_score(100, 0, 0, 0), 0);
    TEST_END();
}
