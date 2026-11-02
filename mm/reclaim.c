/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: reclaim.c
 * Description: Allocation-free registry and dispatcher for memory reclaimers.
 */

#include <ir0/reclaim.h>
#include <ir0/errno.h>
#include <stdint.h>

#define IR0_RECLAIMER_MAX 16

static const struct ir0_reclaimer *reclaimers[IR0_RECLAIMER_MAX];
static uint32_t reclaimer_count;
static uint8_t registry_lock;
static uint8_t reclaim_active;

static void reclaim_lock(uint8_t *lock)
{
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE))
        __asm__ volatile("" ::: "memory");
}

static void reclaim_unlock(uint8_t *lock)
{
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

int mm_reclaimer_register(const struct ir0_reclaimer *reclaimer)
{
    uint32_t i;

    if (!reclaimer || !reclaimer->name || !reclaimer->name[0] ||
        !reclaimer->scan)
        return -EINVAL;

    reclaim_lock(&registry_lock);
    for (i = 0; i < reclaimer_count; i++)
    {
        if (reclaimers[i] == reclaimer)
        {
            reclaim_unlock(&registry_lock);
            return -EEXIST;
        }
    }
    if (reclaimer_count >= IR0_RECLAIMER_MAX)
    {
        reclaim_unlock(&registry_lock);
        return -ENOSPC;
    }
    reclaimers[reclaimer_count++] = reclaimer;
    reclaim_unlock(&registry_lock);
    return 0;
}

int mm_reclaim_run(uint64_t target_pages, struct ir0_reclaim_result *result)
{
    const struct ir0_reclaimer *snapshot[IR0_RECLAIMER_MAX];
    struct ir0_reclaim_result local = {0};
    uint32_t count;
    uint32_t i;
    int rc = 0;

    if (!result || target_pages == 0)
        return -EINVAL;
    if (__atomic_test_and_set(&reclaim_active, __ATOMIC_ACQUIRE))
        return -EBUSY;

    reclaim_lock(&registry_lock);
    count = reclaimer_count;
    for (i = 0; i < count; i++)
        snapshot[i] = reclaimers[i];
    reclaim_unlock(&registry_lock);

    for (i = 0; i < count && local.reclaimed_pages < target_pages; i++)
    {
        uint64_t reclaimed = 0;
        uint64_t remaining = target_pages - local.reclaimed_pages;
        const struct ir0_reclaimer *reclaimer = snapshot[i];

        if (reclaimer->count)
        {
            uint64_t available = reclaimer->count(reclaimer->ctx);

            if (UINT64_MAX - local.reclaimable_pages < available)
                local.reclaimable_pages = UINT64_MAX;
            else
                local.reclaimable_pages += available;
            if (available == 0)
                continue;
        }

        local.reclaimers_scanned++;
        rc = reclaimer->scan(reclaimer->ctx, remaining, &reclaimed);
        if (rc != 0)
        {
            local.reclaimers_failed++;
            continue;
        }
        if (reclaimed > remaining)
            reclaimed = remaining;
        local.reclaimed_pages += reclaimed;
    }

    *result = local;
    __atomic_clear(&reclaim_active, __ATOMIC_RELEASE);
    return 0;
}
