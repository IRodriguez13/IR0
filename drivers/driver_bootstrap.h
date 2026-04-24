/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Driver bootstrap orchestration
 */

#pragma once

#include <stdint.h>

typedef enum {
    DRIVER_BOOT_STAGE_INPUT = 0,
    DRIVER_BOOT_STAGE_PLATFORM,
    DRIVER_BOOT_STAGE_STORAGE,
    DRIVER_BOOT_STAGE_AUDIO,
    DRIVER_BOOT_STAGE_NETWORK,
    DRIVER_BOOT_STAGE_MAX
} driver_boot_stage_t;

typedef int (*driver_boot_init_fn)(void);

int driver_bootstrap_register(driver_boot_stage_t stage, const char *name,
                              driver_boot_init_fn init_fn, int enabled);
int driver_bootstrap_run_all(void);
void driver_bootstrap_reset(void);

