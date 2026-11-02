/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: oom.c
 * Description: Linux-inspired, architecture-neutral OOM victim policy.
 */

#include <ir0/oom.h>
#include <ir0/errno.h>
#include <stdint.h>

static uint64_t oom_adjustment_pages(uint64_t total_pages, unsigned adjustment)
{
    uint64_t whole;
    uint64_t remainder;

    whole = (total_pages / IR0_OOM_SCORE_ADJ_MAX) * adjustment;
    remainder = ((total_pages % IR0_OOM_SCORE_ADJ_MAX) * adjustment) /
                IR0_OOM_SCORE_ADJ_MAX;
    if (UINT64_MAX - whole < remainder)
        return UINT64_MAX;
    return whole + remainder;
}

int oom_candidate_eligible(const struct ir0_oom_candidate *candidate)
{
    const uint32_t required = IR0_OOM_CANDIDATE_USER |
                              IR0_OOM_CANDIDATE_ALIVE |
                              IR0_OOM_CANDIDATE_HAS_MM;

    if (!candidate || candidate->pid <= 1)
        return 0;
    if ((candidate->flags & required) != required)
        return 0;
    if (candidate->flags & IR0_OOM_CANDIDATE_SKIP)
        return 0;
    if (candidate->score_adj <= IR0_OOM_SCORE_ADJ_MIN)
        return 0;
    return 1;
}

uint64_t oom_candidate_badness(const struct ir0_oom_candidate *candidate,
                               uint64_t total_pages)
{
    uint64_t points;
    uint64_t adjustment;

    if (!oom_candidate_eligible(candidate))
        return 0;
    if (candidate->flags & IR0_OOM_CANDIDATE_ORIGIN)
        return UINT64_MAX;

    points = candidate->resident_pages;
    if (UINT64_MAX - points < candidate->page_table_pages)
        points = UINT64_MAX;
    else
        points += candidate->page_table_pages;

    adjustment = oom_adjustment_pages(total_pages,
                                      candidate->score_adj < 0
                                          ? (unsigned)-candidate->score_adj
                                          : (unsigned)candidate->score_adj);
    if (candidate->score_adj < 0)
        points = adjustment >= points ? 0 : points - adjustment;
    else if (UINT64_MAX - points < adjustment)
        points = UINT64_MAX;
    else
        points += adjustment;

    /* Zero is reserved for ineligible candidates. */
    return points == UINT64_MAX ? UINT64_MAX : points + 1;
}

int oom_select_candidate(const struct ir0_oom_candidate *candidates,
                         size_t count, uint64_t total_pages,
                         size_t *selected_index)
{
    uint64_t best_score = 0;
    int32_t best_pid = 0;
    size_t best = 0;
    size_t i;

    if (!candidates || !selected_index)
        return -EINVAL;

    for (i = 0; i < count; i++)
    {
        uint64_t score = oom_candidate_badness(&candidates[i], total_pages);

        if (score == 0)
            continue;
        if (score > best_score ||
            (score == best_score &&
             (best_pid == 0 || candidates[i].pid < best_pid)))
        {
            best = i;
            best_pid = candidates[i].pid;
            best_score = score;
        }
    }

    if (best_score == 0)
        return -ESRCH;
    *selected_index = best;
    return 0;
}
