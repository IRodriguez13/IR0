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

#include <ir0/driver_bootstrap.h>
#include <ir0/ktm/klog.h>
#include <stddef.h>

static int driver_boot_run_entry(const struct driver_boot_entry *entry)
{
    int ret;

    klog_debug_fmt("DRIVERS", "Boot init: %s", entry->name);
    ret = entry->init();
    if (ret != 0)
    {
        klog_warn_fmt("DRIVERS", "Boot init failed: %s (%d)",
                        entry->name, ret);
        return -1;
    }
    klog_info_fmt("DRIVERS", "%s initialized", entry->name);
    return 0;
}

int driver_bootstrap_run(const struct driver_boot_table *const *tables,
                         size_t ntables)
{
    int stage;
    size_t t;
    size_t i;
    int failures = 0;

    for (stage = 0; stage < DRIVER_BOOT_STAGE_MAX; stage++)
    {
        for (t = 0; t < ntables; t++)
        {
            for (i = 0; i < tables[t]->count; i++)
            {
                const struct driver_boot_entry *entry = &tables[t]->entries[i];

                if ((int)entry->stage != stage || !entry->enabled ||
                    !entry->name || !entry->init)
                    continue;
                if (driver_boot_run_entry(entry) != 0)
                    failures++;
            }
        }
    }

    return (failures == 0) ? 0 : -failures;
}

