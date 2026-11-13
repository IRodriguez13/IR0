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

int oom_candidate_better(const struct ir0_oom_candidate *candidate,
                         const struct ir0_oom_candidate *incumbent,
                         uint64_t total_pages)
{
    uint64_t score;
    uint64_t best;

    if (!candidate)
        return 0;
    score = oom_candidate_badness(candidate, total_pages);
    if (score == 0)
        return 0;
    if (!incumbent)
        return 1;

    best = oom_candidate_badness(incumbent, total_pages);
    if (score != best)
        return score > best;
    /* Deterministic tie-break: the lowest PID is the older process. */
    return candidate->pid < incumbent->pid;
}

int oom_select_candidate(const struct ir0_oom_candidate *candidates,
                         size_t count, uint64_t total_pages,
                         size_t *selected_index)
{
    size_t best = 0;
    int found = 0;
    size_t i;

    if (!candidates || !selected_index)
        return -EINVAL;

    for (i = 0; i < count; i++)
    {
        if (!oom_candidate_better(&candidates[i],
                                  found ? &candidates[best] : NULL,
                                  total_pages))
            continue;
        best = i;
        found = 1;
    }

    if (!found)
        return -ESRCH;
    *selected_index = best;
    return 0;
}

uint64_t oom_badness_score(uint64_t resident_pages, uint64_t page_table_pages,
                           int score_adj, uint64_t total_pages)
{
    struct ir0_oom_candidate candidate = {
        .pid = 2, /* any PID above init so eligibility admits the query */
        .flags = IR0_OOM_CANDIDATE_USER | IR0_OOM_CANDIDATE_ALIVE |
                 IR0_OOM_CANDIDATE_HAS_MM,
        .resident_pages = resident_pages,
        .page_table_pages = page_table_pages,
    };
    uint64_t badness;

    if (total_pages == 0)
        return 0;
    if (score_adj < IR0_OOM_SCORE_ADJ_MIN)
        score_adj = IR0_OOM_SCORE_ADJ_MIN;
    if (score_adj > IR0_OOM_SCORE_ADJ_MAX)
        score_adj = IR0_OOM_SCORE_ADJ_MAX;
    candidate.score_adj = (int16_t)score_adj;

    badness = oom_candidate_badness(&candidate, total_pages);
    if (badness == 0)
        return 0;
    if (badness == UINT64_MAX || badness >= total_pages)
        return 1000;
    if (badness > UINT64_MAX / 1000)
        return 1000;
    return (badness * 1000) / total_pages;
}
