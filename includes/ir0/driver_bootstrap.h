/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Driver bootstrap orchestration facade (no drivers/ include).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum
{
	DRIVER_BOOT_STAGE_INPUT = 0,
	DRIVER_BOOT_STAGE_PLATFORM,
	DRIVER_BOOT_STAGE_STORAGE,
	DRIVER_BOOT_STAGE_AUDIO,
	DRIVER_BOOT_STAGE_NETWORK,
	DRIVER_BOOT_STAGE_MAX
} driver_boot_stage_t;

typedef int (*driver_boot_init_fn)(void);

/*
 * One boot driver. The table that holds it decides the ISA (generic table in
 * drivers/init_drv.c, platform table from ir0_boot_arch_driver_table);
 * @enabled is the CONFIG_* condition, a compile-time constant.
 */
struct driver_boot_entry
{
	const char *name;
	driver_boot_init_fn init;
	driver_boot_stage_t stage;
	int enabled;
};

struct driver_boot_table
{
	const struct driver_boot_entry *entries;
	size_t count;
};

#define DRIVER_BOOT_TABLE(array) \
	{ (array), sizeof(array) / sizeof((array)[0]) }

/*
 * Run every enabled entry stage by stage; within a stage, @tables in order
 * and each table's entries in order. Returns 0, or -(number of failed inits).
 */
int driver_bootstrap_run(const struct driver_boot_table *const *tables,
			 size_t ntables);
