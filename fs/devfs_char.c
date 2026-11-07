/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_char.c
 * Description: null, zero, random and full device nodes
 */

#include "devfs_internal.h"

int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Always returns EOF */
    return 0;
}

int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    /* Accepts all data, discards it */
    return count;
}

int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    memset(buf, 0, count);
    return count;
}

int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    return count;
}
/**
 * /dev/random and /dev/urandom - Random number generators
 * 
 * Simple PRNG based on timer ticks and interrupt counters
 * For production, should use hardware RNG or better entropy source
 */
static uint32_t random_seed = 0;

static uint32_t simple_rand(void)
{
    /* Linear congruential generator (LCG) */
    random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return random_seed;
}

int64_t dev_random_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Initialize seed with timer if not set */
    if (random_seed == 0)
    {
        random_seed = (uint32_t)(clock_get_uptime_milliseconds() & 0xFFFFFFFF);
    }
    
    /* Fill buffer with random bytes */
    uint8_t *buffer = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
    {
        buffer[i] = (uint8_t)(simple_rand() & 0xFF);
    }
    
    return (int64_t)count;
}

int64_t dev_random_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    /*
     * /dev/random accepts writes to add entropy to the pool.
     * Mix written bytes with random_seed via XOR and LCG feedback.
     */
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        random_seed ^= (uint32_t)p[i] << (i % 24);
        random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    }
    return (int64_t)count;
}

int64_t dev_urandom_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* /dev/urandom: Non-blocking random number generator
     * Unlike /dev/random which may block when entropy is low,
     * /dev/urandom never blocks and continues generating pseudo-random data.
     * 
     * In this implementation, both use the same LCG-based generator,
     * but /dev/urandom explicitly never blocks and always returns immediately.
     */
    
    /* Initialize seed with timer if not set */
    if (random_seed == 0)
    {
        random_seed = (uint32_t)(clock_get_uptime_milliseconds() & 0xFFFFFFFF);
    }
    
    /* Fill buffer with random bytes - non-blocking, always succeeds */
    uint8_t *buffer = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
    {
        /* Use LCG with different multiplier for urandom to differentiate streams */
        random_seed = (random_seed * 1664525 + 1013904223) & 0x7FFFFFFF;
        buffer[i] = (uint8_t)(random_seed & 0xFF);
    }
    
    return (int64_t)count;
}

int64_t dev_urandom_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    /* Same as /dev/random */
    return dev_random_write(entry, buf, count, offset);
}

/**
 * /dev/full - Device that always returns ENOSPC on write
 */
int64_t dev_full_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Reading from /dev/full returns \0 (null bytes) */
    memset(buf, 0, count);
    return (int64_t)count;
}

int64_t dev_full_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Writing to /dev/full always fails with ENOSPC */
    return -ENOSPC;  /* No space left on device */
}
static const devfs_ops_t null_ops = {
    .read = dev_null_read,
    .write = dev_null_write,
};

static const devfs_ops_t zero_ops = {
    .read = dev_zero_read,
    .write = dev_zero_write,
};

static const devfs_ops_t random_ops = {
    .read = dev_random_read,
    .write = dev_random_write,
};

static const devfs_ops_t urandom_ops = {
    .read = dev_urandom_read,
    .write = dev_urandom_write,
};

static const devfs_ops_t full_ops = {
    .read = dev_full_read,
    .write = dev_full_write,
};
devfs_node_t dev_null = {
    .entry = { .name = "null", .mode = 0666, .device_id = 1, .rdev = IR0_MKDEV(1, 3) },
    .ops = &null_ops,
    .ref_count = 0
};

devfs_node_t dev_zero = {
    .entry = { .name = "zero", .mode = 0666, .device_id = 2, .rdev = IR0_MKDEV(1, 5) },
    .ops = &zero_ops,
    .ref_count = 0
};
/*
 * Registers /dev/hda..hdc only when a disk is present, and hdX{N} partitions
 * only when the partition table exposes them (never a fixed hdX4 grid).
 */



devfs_node_t dev_random = {
    .entry = { .name = "random", .mode = 0644, .device_id = 10, .rdev = IR0_MKDEV(1, 8) },
    .ops = &random_ops,
    .ref_count = 0
};

devfs_node_t dev_urandom = {
    .entry = { .name = "urandom", .mode = 0644, .device_id = 11, .rdev = IR0_MKDEV(1, 9) },
    .ops = &urandom_ops,
    .ref_count = 0
};

devfs_node_t dev_full = {
    .entry = { .name = "full", .mode = 0666, .device_id = 12, .rdev = IR0_MKDEV(1, 7) },
    .ops = &full_ops,
    .ref_count = 0
};

