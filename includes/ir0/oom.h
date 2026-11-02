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

/*
 * Ordering primitive shared by the array selector and the kernel's streaming
 * registry walk. Returns non-zero when @candidate must replace @incumbent.
 * A NULL incumbent loses to any eligible candidate; ineligible candidates
 * never win. Ties break on the lowest PID so victim choice is reproducible.
 */
int oom_candidate_better(const struct ir0_oom_candidate *candidate,
                         const struct ir0_oom_candidate *incumbent,
                         uint64_t total_pages);
int oom_select_candidate(const struct ir0_oom_candidate *candidates,
                         size_t count, uint64_t total_pages,
                         size_t *selected_index);

struct process;

/*
 * Per-process Linux oom_score_adj tunable (-1000..1000). IR0_OOM_SCORE_ADJ_MIN
 * makes the process ineligible; values above the range are rejected.
 */
int process_oom_score_adj_get(const struct process *process);
int process_oom_score_adj_set(struct process *process, int adjustment);

/*
 * Kernel integration (mm/oom_kill.c). The policy above is pure and stays
 * host-testable; these entry points own candidate collection, victim
 * signalling and the OOM event counters.
 *
 * oom_handle_alloc_failure() is the allocator hook: it only acts when a user
 * task is allocating, and returns 0 when a victim was killed so the caller may
 * retry (or simply return and let the faulting instruction re-execute).
 */
int oom_kill_process(struct process *victim, const char *reason);
int oom_out_of_memory(struct process *origin, const char *reason);
int oom_handle_alloc_failure(struct process *current, const char *site);
uint64_t oom_kill_count(void);

/* Normalised Linux /proc/<pid>/oom_score (0..1000) from raw accounting. */
uint64_t oom_badness_score(uint64_t resident_pages, uint64_t page_table_pages,
                           int score_adj, uint64_t total_pages);
