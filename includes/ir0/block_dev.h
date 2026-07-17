/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Legacy block_dev name adapter (sealed facade).
 *
 * Public API is ir0_block_* in <ir0/blockdev.h>. These name-based helpers
 * remain for drivers/partition and transitional callers; they do not pull
 * <drivers/...>.
 */

#ifndef IR0_FACADE_BLOCK_DEV_H
#define IR0_FACADE_BLOCK_DEV_H

#include <ir0/blockdev.h>
#include <stdbool.h>
#include <stdint.h>

#define BLOCK_DEV_SECTOR_SIZE 512

bool block_dev_read_sectors(const char *name, uint32_t lba, uint8_t n, void *buf);
bool block_dev_write_sectors(const char *name, uint32_t lba, uint8_t n,
			     const void *buf);
uint64_t block_dev_get_sector_count(const char *name);
bool block_dev_is_present(const char *name);
int block_dev_count(void);
const char *block_dev_name_at(int index);
const char *block_dev_legacy_name(uint8_t disk_id);

/* Deprecated — drivers register via ir0_block_register(). */
int block_dev_register(const char *name, const void *ops, uint8_t dev_id);

#endif /* IR0_FACADE_BLOCK_DEV_H */
