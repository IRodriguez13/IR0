/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: driver_bootstrap.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Driver bootstrap orchestration
 */

#include "driver_bootstrap.h"
#include <ir0/logging.h>
#include <stddef.h>

#define DRIVER_BOOT_MAX_ENTRIES 32

typedef struct {
    driver_boot_stage_t stage;
    const char *name;
    driver_boot_init_fn init_fn;
    int enabled;
} driver_boot_entry_t;

static driver_boot_entry_t g_boot_entries[DRIVER_BOOT_MAX_ENTRIES];
static int g_boot_entry_count;

void driver_bootstrap_reset(void)
{
    g_boot_entry_count = 0;
}

int driver_bootstrap_register(driver_boot_stage_t stage, const char *name,
                              driver_boot_init_fn init_fn, int enabled)
{
    if (!name || !init_fn)
        return -1;
    if (stage >= DRIVER_BOOT_STAGE_MAX)
        return -1;
    if (g_boot_entry_count >= DRIVER_BOOT_MAX_ENTRIES)
        return -1;

    g_boot_entries[g_boot_entry_count].stage = stage;
    g_boot_entries[g_boot_entry_count].name = name;
    g_boot_entries[g_boot_entry_count].init_fn = init_fn;
    g_boot_entries[g_boot_entry_count].enabled = enabled;
    g_boot_entry_count++;
    return 0;
}

int driver_bootstrap_run_all(void)
{
    int stage;
    int i;
    int failures = 0;

    for (stage = 0; stage < DRIVER_BOOT_STAGE_MAX; stage++)
    {
        for (i = 0; i < g_boot_entry_count; i++)
        {
            int ret;
            driver_boot_entry_t *entry = &g_boot_entries[i];

            if ((int)entry->stage != stage || !entry->enabled)
                continue;

            LOG_INFO_FMT("DRIVERS", "Boot init: %s", entry->name);
            ret = entry->init_fn();
            if (ret != 0)
            {
                LOG_WARNING_FMT("DRIVERS", "Boot init failed: %s (%d)",
                                entry->name, ret);
                failures++;
            }
        }
    }

    return (failures == 0) ? 0 : -failures;
}

