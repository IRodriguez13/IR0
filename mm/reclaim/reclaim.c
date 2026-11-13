/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: reclaim.c
 * Description: Allocation-free registry and dispatcher for memory reclaimers.
 */

#include <ir0/reclaim.h>
#include <ir0/atomic.h>
#include <ir0/errno.h>
#include <ir0/spinlock.h>

#define IR0_RECLAIMER_MAX 16

static const struct ir0_reclaimer *reclaimers[IR0_RECLAIMER_MAX];
static uint32_t reclaimer_count;
/* Guards the reclaimer table. */
static ir0_spinlock_t reclaimer_lock;
/* One reclaim pass at a time; a nested caller gets -EBUSY (try-lock). */
static unsigned long reclaim_active;

int mm_reclaimer_register(const struct ir0_reclaimer *reclaimer)
{
    unsigned long irq_flags;
    uint32_t i;

    if (!reclaimer || !reclaimer->name || !reclaimer->name[0] ||
        !reclaimer->scan)
        return -EINVAL;

    irq_flags = ir0_spin_lock_irqsave(&reclaimer_lock);
    for (i = 0; i < reclaimer_count; i++)
    {
        if (reclaimers[i] == reclaimer)
        {
            ir0_spin_unlock_irqrestore(&reclaimer_lock, irq_flags);
            return -EEXIST;
        }
    }
    if (reclaimer_count >= IR0_RECLAIMER_MAX)
    {
        ir0_spin_unlock_irqrestore(&reclaimer_lock, irq_flags);
        return -ENOSPC;
    }
    reclaimers[reclaimer_count++] = reclaimer;
    ir0_spin_unlock_irqrestore(&reclaimer_lock, irq_flags);
    return 0;
}

int mm_reclaim_run(uint64_t target_pages, struct ir0_reclaim_result *result)
{
    unsigned long irq_flags;
    const struct ir0_reclaimer *snapshot[IR0_RECLAIMER_MAX];
    struct ir0_reclaim_result local = {0};
    uint32_t count;
    uint32_t i;
    int rc = 0;

    if (!result || target_pages == 0)
        return -EINVAL;
    if (test_and_set_bit_lock(0, &reclaim_active))
        return -EBUSY;

    irq_flags = ir0_spin_lock_irqsave(&reclaimer_lock);
    count = reclaimer_count;
    for (i = 0; i < count; i++)
        snapshot[i] = reclaimers[i];
    ir0_spin_unlock_irqrestore(&reclaimer_lock, irq_flags);

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
    clear_bit_unlock(0, &reclaim_active);
    return 0;
}
