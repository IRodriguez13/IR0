/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: test_mm_reclaim.c
 * Description: Host contracts for the generic memory reclaimer registry.
 */

#include "test_harness.h"
#include <ir0/reclaim.h>
#include <ir0/errno.h>

struct reclaim_fixture
{
    uint64_t available;
    int nested_rc;
};

static uint64_t fixture_count(void *ctx)
{
    return ((struct reclaim_fixture *)ctx)->available;
}

static int fixture_scan(void *ctx, uint64_t target, uint64_t *reclaimed)
{
    struct reclaim_fixture *fixture = ctx;

    *reclaimed = fixture->available < target ? fixture->available : target;
    fixture->available -= *reclaimed;
    return 0;
}

static int nested_scan(void *ctx, uint64_t target, uint64_t *reclaimed)
{
    struct reclaim_fixture *fixture = ctx;
    struct ir0_reclaim_result nested;

    (void)target;
    *reclaimed = 0;
    fixture->nested_rc = mm_reclaim_run(1, &nested);
    return 0;
}

void test_mm_reclaim_contract(void)
{
    static struct reclaim_fixture first = { .available = 4 };
    static struct reclaim_fixture second = { .available = 8 };
    static struct reclaim_fixture nested;
    static const struct ir0_reclaimer first_reclaimer = {
        .name = "first", .count = fixture_count,
        .scan = fixture_scan, .ctx = &first,
    };
    static const struct ir0_reclaimer second_reclaimer = {
        .name = "second", .count = fixture_count,
        .scan = fixture_scan, .ctx = &second,
    };
    static const struct ir0_reclaimer nested_reclaimer = {
        .name = "nested", .scan = nested_scan, .ctx = &nested,
    };
    struct ir0_reclaim_result result;

    TEST_BEGIN("mm_reclaim_registry_contract");
    ASSERT_EQ(mm_reclaimer_register(NULL), -EINVAL);
    ASSERT_EQ(mm_reclaimer_register(&first_reclaimer), 0);
    ASSERT_EQ(mm_reclaimer_register(&first_reclaimer), -EEXIST);
    ASSERT_EQ(mm_reclaimer_register(&second_reclaimer), 0);
    ASSERT_EQ(mm_reclaimer_register(&nested_reclaimer), 0);
    TEST_END();

    TEST_BEGIN("mm_reclaim_stops_at_target");
    ASSERT_EQ(mm_reclaim_run(6, &result), 0);
    ASSERT_EQ(result.reclaimed_pages, (uint64_t)6);
    ASSERT_EQ(result.reclaimers_scanned, (uint32_t)2);
    ASSERT_EQ(first.available, (uint64_t)0);
    ASSERT_EQ(second.available, (uint64_t)6);
    TEST_END();

    TEST_BEGIN("mm_reclaim_rejects_recursive_entry");
    ASSERT_EQ(mm_reclaim_run(8, &result), 0);
    ASSERT_EQ(nested.nested_rc, -EBUSY);
    ASSERT_EQ(result.reclaimed_pages, (uint64_t)6);
    ASSERT_EQ(result.reclaimers_scanned, (uint32_t)2);
    TEST_END();
}
