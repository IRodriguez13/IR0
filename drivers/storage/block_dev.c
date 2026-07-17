/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Block device registry (legacy name-based adapter).
 *
 * New code should use includes/ir0/blockdev.h (dev_t + ir0_block_*).
 * This module delegates to the blockdev facade for compatibility.
 */

#include "block_dev.h"
#include <ir0/blockdev.h>
#include <string.h>

int block_dev_register(const char *name, const block_dev_ops_t *ops, uint8_t dev_id)
{
	(void)name;
	(void)ops;
	(void)dev_id;
	return -1;
}

const block_dev_ops_t *block_dev_get(const char *name, uint8_t *dev_id_out)
{
	(void)name;
	if (dev_id_out)
		*dev_id_out = 0;
	return NULL;
}

bool block_dev_read_sectors(const char *name, uint32_t lba, uint8_t n, void *buf)
{
	dev_t dev;

	if (!name || !buf || n == 0)
		return false;
	dev = ir0_block_lookup_by_name(name);
	if (dev == 0)
		return false;
	return ir0_block_read(dev, lba, n, buf) == 0;
}

bool block_dev_write_sectors(const char *name, uint32_t lba, uint8_t n, const void *buf)
{
	dev_t dev;

	if (!name || !buf || n == 0)
		return false;
	dev = ir0_block_lookup_by_name(name);
	if (dev == 0)
		return false;
	return ir0_block_write(dev, lba, n, buf) == 0;
}

uint64_t block_dev_get_sector_count(const char *name)
{
	struct ir0_block_info info;
	dev_t dev;

	if (!name)
		return 0;
	dev = ir0_block_lookup_by_name(name);
	if (dev == 0 || ir0_block_get_info(dev, &info) != 0)
		return 0;
	return info.sector_count;
}

bool block_dev_is_present(const char *name)
{
	dev_t dev;

	if (!name)
		return false;
	dev = ir0_block_lookup_by_name(name);
	return dev != 0 && ir0_block_is_present(dev);
}

int block_dev_count(void)
{
	return ir0_block_count();
}

const char *block_dev_name_at(int index)
{
	return ir0_block_name_at(index);
}

const char *block_dev_legacy_name(uint8_t disk_id)
{
	return ir0_block_legacy_name(disk_id);
}
