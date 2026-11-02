/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: reclaim.h
 * Description: Generic memory-pressure reclaimer facade.
 */

#pragma once

#include <stdint.h>

struct ir0_reclaimer
{
    const char *name;
    uint64_t (*count)(void *ctx);
    int (*scan)(void *ctx, uint64_t target_pages, uint64_t *reclaimed_pages);
    void *ctx;
};

struct ir0_reclaim_result
{
    uint64_t reclaimable_pages;
    uint64_t reclaimed_pages;
    uint32_t reclaimers_scanned;
    uint32_t reclaimers_failed;
};

/* Reclaimers have static lifetime and are normally registered during boot. */
int mm_reclaimer_register(const struct ir0_reclaimer *reclaimer);

/*
 * Invoke registered reclaimers without allocating memory. A nested reclaim
 * returns -EBUSY so allocation failure paths cannot recurse indefinitely.
 */
int mm_reclaim_run(uint64_t target_pages, struct ir0_reclaim_result *result);
