/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: oom.h
 * Description: Architecture-neutral out-of-memory selection policy.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_OOM_SCORE_ADJ_MIN (-1000)
#define IR0_OOM_SCORE_ADJ_MAX 1000

enum ir0_oom_candidate_flags
{
    IR0_OOM_CANDIDATE_USER = 1U << 0,
    IR0_OOM_CANDIDATE_ALIVE = 1U << 1,
    IR0_OOM_CANDIDATE_HAS_MM = 1U << 2,
    IR0_OOM_CANDIDATE_SKIP = 1U << 3,
    IR0_OOM_CANDIDATE_ORIGIN = 1U << 4
};

struct ir0_oom_candidate
{
    int32_t pid;
    uint32_t flags;
    uint64_t resident_pages;
    uint64_t page_table_pages;
    int16_t score_adj;
};

int oom_candidate_eligible(const struct ir0_oom_candidate *candidate);
uint64_t oom_candidate_badness(const struct ir0_oom_candidate *candidate,
                               uint64_t total_pages);
int oom_select_candidate(const struct ir0_oom_candidate *candidates,
                         size_t count, uint64_t total_pages,
                         size_t *selected_index);
